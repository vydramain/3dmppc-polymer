// ─── NEUROSLOP ────────────────────────────────────────────────────────────────
// Сгенерировано Claude (claude-opus-5). Не проверено человеком.
// Ревизия: stage 9 — офлайн-конвертер PNG → .mppctex (IDX4/IDX8/DIRECT15).
// ──────────────────────────────────────────────────────────────────────────────
//
// mppctex — the build-time half of the texture pipeline.
//
// The console never decodes an image format, exactly like a real devkit: a disc
// hands FINISHED texels to rv_cv::video_asset_write(). Somebody still has to
// turn artwork into those texels, and that somebody is this tool, run on the
// developer's machine while the game is built. Its output — a .mppctex file —
// is a blob the disc can read and upload almost verbatim; see tools/README.md
// for the byte layout and for the disc-side loading recipe.
//
// Everything about the 16-bit value below is dictated by pdk/cv/rv_texture.hpp:
//   bits 0-4 R, 5-9 G, 10-14 B, bit 15 STP, and 0000h == FULLY TRANSPARENT.

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

namespace {

// --- the file format ----------------------------------------------------------

// Header, little-endian, 16 bytes. Kept at 16 so the palette that follows it is
// 2-byte aligned in the file and a disc may point a `const uint16_t*` straight
// at it after a single read.
constexpr char kMagic[4] = {'M', 'P', 'T', 'X'};
constexpr uint16_t kVersion = 1;
constexpr size_t kHeaderSize = 16;

// The one 16-bit encoding shared by DIRECT15 texels and palette entries.
constexpr uint16_t kTransparent = 0x0000;  // rv_texture.hpp: not drawn at all
constexpr uint16_t kNearBlack = 0x0001;    // darkest red — the opaque stand-in

// --- small helpers ------------------------------------------------------------

struct rgb8 {
    uint8_t r = 0;
    uint8_t g = 0;
    uint8_t b = 0;
};

// A colour already reduced to the console's 5-bit-per-channel space. All
// quantisation happens HERE and not in 8-bit space: the palette entries can only
// ever hold 5 bits per channel, so measuring distances in 8-bit space would
// optimise a precision the hardware throws away a moment later.
struct rgb5 {
    uint8_t r = 0;  // 0..31
    uint8_t g = 0;  // 0..31
    uint8_t b = 0;  // 0..31
};

// 8-bit -> 5-bit with rounding to the nearest representable level. (v * 31 +
// 127) / 255 maps 0 -> 0 and 255 -> 31 without the dark bias a plain >> 3 has.
uint8_t to5(uint8_t v) { return static_cast<uint8_t>((static_cast<int>(v) * 31 + 127) / 255); }

rgb5 to5(rgb8 c) { return rgb5{to5(c.r), to5(c.g), to5(c.b)}; }

// Pack into the layout rv_texture.hpp mandates. STP is left clear: the
// semi-transparency modes are DEFERRED on the console side, and a stored STP
// bit would silently change the look once they land.
uint16_t pack(rgb5 c) {
    return static_cast<uint16_t>((c.r & 0x1F) | ((c.g & 0x1F) << 5) | ((c.b & 0x1F) << 10));
}

// THEOREM: the opaque-black trap — 0000h is the transparency sentinel, so an
// OPAQUE pixel may never encode to it. Naive quantisation sends every very dark
// pixel to 0000h and the picture's shadows turn into holes; this is the classic
// PSX mistake and it is only visible once the sprite is on screen over a
// background. The fix is one line applied at every point where an opaque colour
// becomes a 16-bit word: nudge 0000h to 0001h (darkest red, one 1/31 step of red
// away — below the console's own dithering noise floor).
uint16_t pack_opaque(rgb5 c) {
    const uint16_t v = pack(c);
    return v == kTransparent ? kNearBlack : v;
}

// --- source image -------------------------------------------------------------

// One decoded pixel of the source, already reduced to console precision, plus
// the verdict on whether it is a hole.
struct src_pixel {
    rgb5 color;
    bool transparent = false;
};

// --- median cut ---------------------------------------------------------------

// THEOREM: median cut (Heckbert 1982) — build the palette by repeatedly halving
// the colour set instead of searching for optimal centroids.
//
// Start with one box holding every distinct opaque colour of the image; while
// fewer boxes than palette slots exist, take the box with the largest channel
// spread, sort its colours along that channel and split it at the MEDIAN. Each
// final box contributes one palette entry: the mean of its colours, weighted by
// how many pixels use them.
//
// Chosen over k-means for three reasons. It is deterministic — the same PNG
// always yields byte-identical output, which is what makes a build cache and a
// diff of committed assets meaningful. It has no seeding problem, so it cannot
// waste palette slots on an empty cluster the way random-seeded k-means can. And
// splitting at the median rather than the mean makes it population-driven: a
// colour covering half the image gets half the palette even if it sits in a
// visually tiny corner of the cube — exactly right for the flat-shaded, few-hues
// artwork this console is aimed at.
//
// Its known weakness (boxes are axis-aligned, so a diagonal colour ramp is cut
// clumsily) is repaired afterwards by a handful of Lloyd iterations, which are
// cheap once median cut has provided a sane starting point.

struct color_bin {
    rgb5 color;
    uint32_t count = 0;  // how many source pixels carry this exact colour
};

struct box {
    size_t begin = 0;  // half-open range over the bin array
    size_t end = 0;
};

uint8_t channel_of(const rgb5& c, int axis) { return axis == 0 ? c.r : (axis == 1 ? c.g : c.b); }

// Widest channel of a box, and how wide it is.
std::pair<int, int> widest_axis(const std::vector<color_bin>& bins, const box& b) {
    int best_axis = 0;
    int best_span = -1;
    for (int axis = 0; axis < 3; ++axis) {
        int lo = 255;
        int hi = -1;
        for (size_t i = b.begin; i < b.end; ++i) {
            const int v = channel_of(bins[i].color, axis);
            lo = std::min(lo, v);
            hi = std::max(hi, v);
        }
        const int span = hi - lo;
        if (span > best_span) {
            best_span = span;
            best_axis = axis;
        }
    }
    return {best_axis, best_span};
}

std::vector<rgb5> median_cut(std::vector<color_bin> bins, size_t want) {
    std::vector<rgb5> palette;
    if (bins.empty() || want == 0) {
        return palette;
    }

    std::vector<box> boxes{box{0, bins.size()}};
    while (boxes.size() < want) {
        // Pick the box that is worst off: widest spread, ties broken by pixel
        // population so a large flat area is refined before a stray gradient.
        size_t target = boxes.size();
        int target_span = 0;
        uint64_t target_pop = 0;
        for (size_t i = 0; i < boxes.size(); ++i) {
            if (boxes[i].end - boxes[i].begin < 2) {
                continue;  // a single colour cannot be split further
            }
            const int span = widest_axis(bins, boxes[i]).second;
            uint64_t pop = 0;
            for (size_t k = boxes[i].begin; k < boxes[i].end; ++k) {
                pop += bins[k].count;
            }
            if (span > target_span || (span == target_span && pop > target_pop)) {
                target = i;
                target_span = span;
                target_pop = pop;
            }
        }
        if (target == boxes.size() || target_span == 0) {
            break;  // every box is a single colour: the image has fewer colours
        }

        box& b = boxes[target];
        const int axis = widest_axis(bins, b).first;
        std::sort(bins.begin() + static_cast<ptrdiff_t>(b.begin),
                  bins.begin() + static_cast<ptrdiff_t>(b.end),
                  [axis](const color_bin& x, const color_bin& y) {
                      return channel_of(x.color, axis) < channel_of(y.color, axis);
                  });

        // Split where the CUMULATIVE PIXEL COUNT crosses half — the median by
        // population, not by number of distinct colours.
        uint64_t total = 0;
        for (size_t i = b.begin; i < b.end; ++i) {
            total += bins[i].count;
        }
        uint64_t acc = 0;
        size_t split = b.begin + 1;
        for (size_t i = b.begin; i + 1 < b.end; ++i) {
            acc += bins[i].count;
            if (acc * 2 >= total) {
                split = i + 1;
                break;
            }
            split = i + 2;
        }
        split = std::clamp(split, b.begin + 1, b.end - 1);

        const box right{split, b.end};
        b.end = split;
        boxes.push_back(right);
    }

    for (const box& b : boxes) {
        uint64_t weight = 0;
        uint64_t sr = 0;
        uint64_t sg = 0;
        uint64_t sb = 0;
        for (size_t i = b.begin; i < b.end; ++i) {
            const uint64_t w = bins[i].count;
            weight += w;
            sr += static_cast<uint64_t>(bins[i].color.r) * w;
            sg += static_cast<uint64_t>(bins[i].color.g) * w;
            sb += static_cast<uint64_t>(bins[i].color.b) * w;
        }
        if (weight == 0) {
            continue;
        }
        palette.push_back(rgb5{static_cast<uint8_t>((sr + weight / 2) / weight),
                               static_cast<uint8_t>((sg + weight / 2) / weight),
                               static_cast<uint8_t>((sb + weight / 2) / weight)});
    }
    return palette;
}

uint32_t distance2(const rgb5& a, const rgb5& b) {
    // THEOREM: luma-weighted squared distance — the eye resolves green far
    // better than blue, so a plain RGB metric spends palette accuracy where it
    // is not seen. The 3/6/1 weights are the cheap integer stand-in for the
    // luminance coefficients, and integers keep the whole search exact and
    // therefore reproducible across compilers.
    const int dr = static_cast<int>(a.r) - static_cast<int>(b.r);
    const int dg = static_cast<int>(a.g) - static_cast<int>(b.g);
    const int db = static_cast<int>(a.b) - static_cast<int>(b.b);
    return static_cast<uint32_t>(3 * dr * dr + 6 * dg * dg + 1 * db * db);
}

size_t nearest(const std::vector<rgb5>& palette, const rgb5& c) {
    size_t best = 0;
    uint32_t best_d = UINT32_MAX;
    for (size_t i = 0; i < palette.size(); ++i) {
        const uint32_t d = distance2(palette[i], c);
        if (d < best_d) {
            best_d = d;
            best = i;
        }
    }
    return best;
}

// THEOREM: Lloyd relaxation — median cut's boxes are axis-aligned, so a colour
// ramp lying diagonally in the cube is split badly. Reassigning every colour to
// its nearest entry and moving each entry to the weighted mean of what it
// received strictly reduces total error per pass and converges quickly; a fixed
// small number of passes is used so the tool stays deterministic and terminates
// in bounded time, which matters when it runs on every build.
void refine(const std::vector<color_bin>& bins, std::vector<rgb5>& palette, int passes) {
    if (palette.empty()) {
        return;
    }
    for (int pass = 0; pass < passes; ++pass) {
        std::vector<uint64_t> weight(palette.size(), 0);
        std::vector<uint64_t> sr(palette.size(), 0);
        std::vector<uint64_t> sg(palette.size(), 0);
        std::vector<uint64_t> sb(palette.size(), 0);
        for (const color_bin& bin : bins) {
            const size_t k = nearest(palette, bin.color);
            weight[k] += bin.count;
            sr[k] += static_cast<uint64_t>(bin.color.r) * bin.count;
            sg[k] += static_cast<uint64_t>(bin.color.g) * bin.count;
            sb[k] += static_cast<uint64_t>(bin.color.b) * bin.count;
        }
        for (size_t i = 0; i < palette.size(); ++i) {
            if (weight[i] == 0) {
                continue;  // an orphaned entry is left where it is, not moved
            }
            palette[i] = rgb5{static_cast<uint8_t>((sr[i] + weight[i] / 2) / weight[i]),
                              static_cast<uint8_t>((sg[i] + weight[i] / 2) / weight[i]),
                              static_cast<uint8_t>((sb[i] + weight[i] / 2) / weight[i])};
        }
    }
}

// --- command line -------------------------------------------------------------

struct options {
    std::string input;
    std::string output;
    int format = 0;        // 4, 8 or 15 — the rv_texfmt value
    bool has_key = false;  // --transparent-key was given
    rgb8 key;              // the colour that becomes a hole
};

[[noreturn]] void die(const std::string& message) {
    std::fprintf(stderr, "mppctex: error: %s\n", message.c_str());
    std::exit(1);
}

void usage(std::FILE* out) {
    std::fprintf(out,
                 "usage: mppctex <input.png> <output.mppctex> --format idx4|idx8|direct15\n"
                 "                [--transparent-key RRGGBB]\n"
                 "\n"
                 "  --format            texel encoding (rv_texfmt): 4-bit or 8-bit palette\n"
                 "                      index, or 15-bit direct colour.\n"
                 "  --transparent-key   source colour (hex, e.g. FF00FF) to encode as the\n"
                 "                      fully transparent value 0000h. PNG alpha < 128 is\n"
                 "                      treated as transparent as well, always.\n");
}

bool parse_hex_rgb(std::string_view text, rgb8* out) {
    if (!text.empty() && text.front() == '#') {
        text.remove_prefix(1);
    }
    if (text.size() != 6) {
        return false;
    }
    uint32_t value = 0;
    for (const char c : text) {
        const unsigned char u = static_cast<unsigned char>(c);
        int digit = 0;
        if (std::isdigit(u)) {
            digit = c - '0';
        } else if (std::isxdigit(u)) {
            digit = std::tolower(u) - 'a' + 10;
        } else {
            return false;
        }
        value = (value << 4) | static_cast<uint32_t>(digit);
    }
    out->r = static_cast<uint8_t>((value >> 16) & 0xFF);
    out->g = static_cast<uint8_t>((value >> 8) & 0xFF);
    out->b = static_cast<uint8_t>(value & 0xFF);
    return true;
}

options parse_args(int argc, char** argv) {
    options opt;
    std::vector<std::string> positional;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            usage(stdout);
            std::exit(0);
        } else if (arg == "--format") {
            if (++i >= argc) {
                die("--format needs a value (idx4, idx8 or direct15)");
            }
            const std::string value = argv[i];
            if (value == "idx4") {
                opt.format = 4;
            } else if (value == "idx8") {
                opt.format = 8;
            } else if (value == "direct15") {
                opt.format = 15;
            } else {
                die("unknown format '" + value + "'; expected idx4, idx8 or direct15");
            }
        } else if (arg == "--transparent-key") {
            if (++i >= argc) {
                die("--transparent-key needs a value (six hex digits, e.g. FF00FF)");
            }
            if (!parse_hex_rgb(argv[i], &opt.key)) {
                die("'" + std::string(argv[i]) + "' is not six hex digits (e.g. FF00FF)");
            }
            opt.has_key = true;
        } else if (arg.size() > 1 && arg[0] == '-') {
            die("unknown option '" + arg + "' (try --help)");
        } else {
            positional.push_back(arg);
        }
    }

    if (positional.size() != 2) {
        usage(stderr);
        die("expected exactly one input and one output path");
    }
    if (opt.format == 0) {
        die("--format is required (idx4, idx8 or direct15)");
    }
    opt.input = positional[0];
    opt.output = positional[1];
    return opt;
}

// --- output -------------------------------------------------------------------

void put_u16(std::vector<uint8_t>& out, uint16_t v) {
    out.push_back(static_cast<uint8_t>(v & 0xFF));
    out.push_back(static_cast<uint8_t>(v >> 8));
}

}  // namespace

int main(int argc, char** argv) {
    const options opt = parse_args(argc, argv);

    int width = 0;
    int height = 0;
    int source_channels = 0;
    // Force RGBA: a source without alpha simply comes back fully opaque, which
    // removes every "does this PNG have alpha" branch from the code below.
    stbi_uc* pixels = stbi_load(opt.input.c_str(), &width, &height, &source_channels, 4);
    if (pixels == nullptr) {
        const char* reason = stbi_failure_reason();
        die("cannot read '" + opt.input + "': " + (reason != nullptr ? reason : "unknown"));
    }
    if (width <= 0 || height <= 0) {
        stbi_image_free(pixels);
        die("'" + opt.input + "' has a zero dimension");
    }
    // The header stores the dimensions as uint16, and the console's own limit is
    // far lower still (rv_cv::texture_max_width/height, 256 on the reference
    // machine). Refuse here rather than write a file no disc can upload.
    if (width > 65535 || height > 65535) {
        stbi_image_free(pixels);
        die("'" + opt.input + "' is larger than 65535 texels on an axis");
    }

    const size_t texel_count = static_cast<size_t>(width) * static_cast<size_t>(height);

    // Reduce the source once: every later stage works on 5-bit colour plus the
    // transparency verdict, never on the raw bytes.
    std::vector<src_pixel> src(texel_count);
    size_t transparent_count = 0;
    for (size_t i = 0; i < texel_count; ++i) {
        const stbi_uc* p = pixels + i * 4;
        const rgb8 c{p[0], p[1], p[2]};
        const bool by_alpha = p[3] < 128;
        const bool by_key = opt.has_key && c.r == opt.key.r && c.g == opt.key.g && c.b == opt.key.b;
        src[i].transparent = by_alpha || by_key;
        src[i].color = to5(c);
        if (src[i].transparent) {
            ++transparent_count;
        }
    }
    stbi_image_free(pixels);

    const bool needs_hole = transparent_count > 0;

    std::vector<uint8_t> file;
    file.insert(file.end(), kMagic, kMagic + 4);
    put_u16(file, kVersion);
    put_u16(file, static_cast<uint16_t>(opt.format));
    put_u16(file, static_cast<uint16_t>(width));
    put_u16(file, static_cast<uint16_t>(height));

    if (opt.format == 15) {
        put_u16(file, 0);  // palette entry count: DIRECT15 carries no palette
        put_u16(file, 0);  // reserved
        if (file.size() != kHeaderSize) {
            die("internal: header size drifted from the documented layout");
        }
        file.reserve(file.size() + texel_count * 2);
        for (const src_pixel& s : src) {
            put_u16(file, s.transparent ? kTransparent : pack_opaque(s.color));
        }
    } else {
        const size_t palette_size = (opt.format == 4) ? 16u : 256u;
        // PATTERN: reserved slot — index 0 is the hole whenever the image has
        // one. Transparency for an indexed format lives in the PALETTE (see
        // rv_texture.hpp: "transparency is decided AFTER the palette lookup"),
        // so a fixed 0000h entry is what a cut-out sprite indexes into. It costs
        // one colour, hence the reservation only happens when it is needed.
        const size_t reserved = needs_hole ? 1u : 0u;
        const size_t color_slots = palette_size - reserved;

        // Histogram of the OPAQUE colours only: transparent pixels carry
        // whatever RGB the artist left under the alpha and must not drag the
        // palette towards it.
        std::vector<color_bin> bins;
        {
            // 15-bit colour space is small enough to bucket directly — an exact
            // histogram with no hashing and no ordering ambiguity.
            std::vector<uint32_t> counts(1u << 15, 0);
            for (const src_pixel& s : src) {
                if (!s.transparent) {
                    ++counts[pack(s.color)];
                }
            }
            for (uint32_t code = 0; code < counts.size(); ++code) {
                if (counts[code] != 0) {
                    bins.push_back(color_bin{rgb5{static_cast<uint8_t>(code & 0x1F),
                                                  static_cast<uint8_t>((code >> 5) & 0x1F),
                                                  static_cast<uint8_t>((code >> 10) & 0x1F)},
                                             counts[code]});
                }
            }
        }
        if (bins.empty()) {
            die("'" + opt.input + "' has no opaque pixel: there is nothing to put in a palette");
        }
        if (bins.size() > color_slots) {
            std::fprintf(stderr,
                         "mppctex: note: %zu distinct colours reduced to %zu palette entries\n",
                         bins.size(), color_slots);
        }

        std::vector<rgb5> palette = median_cut(bins, color_slots);
        refine(bins, palette, 4);

        // Index every source pixel. Done AFTER refinement so the assignment
        // matches the palette actually written to the file.
        std::vector<uint8_t> indices(texel_count, 0);
        for (size_t i = 0; i < texel_count; ++i) {
            if (src[i].transparent) {
                indices[i] = 0;  // the reserved hole; `reserved` is 1 here
                continue;
            }
            indices[i] = static_cast<uint8_t>(nearest(palette, src[i].color) + reserved);
        }

        put_u16(file, static_cast<uint16_t>(palette_size));
        put_u16(file, 0);  // reserved
        if (file.size() != kHeaderSize) {
            die("internal: header size drifted from the documented layout");
        }

        // The palette is always written at full length (16 or 256 entries) so
        // the disc can upload it as one fixed-shape rv_texture without caring
        // how many colours the image actually needed. Unused tail entries are
        // 0000h, which is the safe value: an index that should never be sampled
        // draws nothing instead of a wrong colour.
        if (needs_hole) {
            put_u16(file, kTransparent);
        }
        for (const rgb5& c : palette) {
            put_u16(file, pack_opaque(c));
        }
        for (size_t i = reserved + palette.size(); i < palette_size; ++i) {
            put_u16(file, kTransparent);
        }

        if (opt.format == 8) {
            file.insert(file.end(), indices.begin(), indices.end());
        } else {
            // PATTERN: PSX nibble order — the LOW nibble holds the LEFT texel.
            // The console samples IDX4 the same way, and getting this backwards
            // produces an image that looks almost right, which is the worst kind
            // of wrong. Rows are byte-aligned: an odd width pads its last byte
            // with a zero high nibble, so a row always starts on a byte
            // boundary and the disc's stride is (width + 1) / 2.
            const size_t stride = (static_cast<size_t>(width) + 1) / 2;
            file.reserve(file.size() + stride * static_cast<size_t>(height));
            for (int y = 0; y < height; ++y) {
                const size_t row = static_cast<size_t>(y) * static_cast<size_t>(width);
                for (size_t x = 0; x < static_cast<size_t>(width); x += 2) {
                    const uint8_t low = static_cast<uint8_t>(indices[row + x] & 0x0F);
                    const uint8_t high = (x + 1 < static_cast<size_t>(width))
                                             ? static_cast<uint8_t>(indices[row + x + 1] & 0x0F)
                                             : uint8_t{0};
                    file.push_back(static_cast<uint8_t>(low | (high << 4)));
                }
            }
        }
    }

    std::FILE* out = std::fopen(opt.output.c_str(), "wb");
    if (out == nullptr) {
        die("cannot open '" + opt.output + "' for writing");
    }
    const size_t written = std::fwrite(file.data(), 1, file.size(), out);
    const bool closed = std::fclose(out) == 0;
    if (written != file.size() || !closed) {
        die("failed to write '" + opt.output + "' (disk full?)");
    }

    std::printf("mppctex: %s -> %s (%dx%d, %s, %zu bytes)\n", opt.input.c_str(), opt.output.c_str(),
                width, height, opt.format == 4 ? "idx4" : (opt.format == 8 ? "idx8" : "direct15"),
                file.size());
    return 0;
}
