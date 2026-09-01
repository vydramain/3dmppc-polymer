#include <getopt.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cerrno>
#include <cstdio>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "pdk/cv/rv_texel.hpp"
#include "pdk/cv/rv_texture.hpp"
#include "pdk/cv/rv_vertex.hpp"
#include "pdk/rv_err.hpp"
#include "pdklib/rv_stdio/rv_stdio.hpp"
#include "pdklib/rv_textures/rv_texel_pack.hpp"
#include "pdklib/rv_textures/rv_texfmt_name.hpp"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

// A HOST program, not part of the machine: rv_pdktools is where the author's
// tools live. What it takes from outside is everything the CONSOLE dictates and
// the baker must reproduce byte for byte — the colour types from pdk/, the texel
// layout and the 8-to-5 quantiser from pdklib/rv_textures. A second spelling of
// any of those is a bug waiting for the day someone changes one copy. What
// remains below describes the .mppctex CONTAINER, which no console ever opens,
// and is therefore stated here.
namespace rv_pdktools
{
namespace
{

// --- the file format ----------------------------------------------------------

// Header, little-endian, 16 bytes. Kept at 16 so the palette that follows it is
// 2-byte aligned in the file and a disc may point a `const uint16_t*` straight
// at it after a single read.
constexpr char kMagic[4] = { 'M', 'P', 'T', 'X' };
constexpr uint16_t kVersion = 1;
constexpr size_t kHeaderSize = 16;

// A palette is written at FULL length whatever the image needed — 16 entries for
// IDX4, 256 for IDX8 — so the disc uploads one fixed-shape rv_texture without
// caring how many colours the artwork used. The two sizes are what 4 and 8 index
// bits can address.
constexpr size_t kPaletteSizeIdx4 = 16;
constexpr size_t kPaletteSizeIdx8 = 256;

// PATTERN: reserved slot — index 0 is the hole whenever the image has one.
// Transparency in an indexed format lives in the PALETTE (rv_texture.hpp:
// "transparency is decided AFTER the palette lookup"), so a cut-out sprite
// indexes a fixed 0000h entry. It costs one colour, hence it is only reserved
// when the image actually has transparent pixels.
constexpr uint8_t kHoleIndex = 0;

// PSX nibble order for IDX4: the LOW nibble holds the LEFT texel. The console
// samples it the same way, and getting this backwards produces an image that
// looks almost right — the worst kind of wrong. Rows stay byte-aligned, so an
// odd width pads its last byte with a zero high nibble and the disc's stride is
// (width + 1) / 2.
constexpr int kNibbleBits = 4;
constexpr uint8_t kNibbleMask = 0x0F;

// Exit codes, kept apart so a build script can tell a bad invocation from a bad
// asset: 1 is a job that could not be done, 2 a command line not understood.
// These belong to the process, so only the CLI boundary ever produces them.
constexpr int kExitSuccess = 0;
constexpr int kExitFailure = 1;
constexpr int kExitUsage = 2;

// --- small helpers ------------------------------------------------------------

// The colour vocabulary is the console's, not this tool's: rv_color is the 8-bit
// triple a PNG pixel becomes, rv_color5 the 5-bit one everything downstream
// measures in, and rv_texel_* the layout and the quantiser both ends agree on.
// Named here so the bodies below read as they did when the tool owned them.
using rv_pdk::rv_color;
using rv_pdk::rv_color5;
using rv_pdk::rv_err;
using rv_pdk::RV_ERR_INVAL;
using rv_pdk::RV_ERR_IO;
using rv_pdk::RV_OK;
using rv_pdk::rv_texfmt;
using rv_pdk::RV_TEXFMT_DIRECT15;
using rv_pdk::RV_TEXFMT_IDX4;
using rv_pdk::RV_TEXFMT_IDX8;
using rv_pdklib::rv_texel_opaque;
using rv_pdklib::rv_texel_pack;
using rv_pdklib::rv_texel_quantize;
using rv_pdklib::rv_texel_unpack;

// THEOREM: the opaque-black trap — 0000h is the transparency sentinel, so an
// OPAQUE pixel may never encode to it. Naive quantisation sends every very dark
// pixel there and the picture's shadows turn into holes. rv_texel_opaque is that
// fix, applied wherever an opaque colour becomes a 16-bit word.

// --- source image -------------------------------------------------------------

// stb_image is asked for this many channels whatever the PNG holds, so a source
// without alpha comes back fully opaque and every "does this file have alpha"
// branch disappears from the code below.
constexpr int kSourceChannels = 4;

// The console does not blend: a texel is either drawn or it is a hole. Anything
// below half opacity becomes a hole. The exact cut is arbitrary — it only has to
// be fixed, so the same PNG always bakes the same way.
constexpr uint8_t kAlphaTransparentBelow = 128;

// Width and height are stored as uint16 in the header, so nothing larger can be
// described by the container at all. This is the CONTAINER's limit, not the
// machine's: the console's own texture_max_width/height (256 on the reference
// machine) is per-disc configuration this tool never sees, and it is enforced
// only when the texture is uploaded — rv_pccv::video_asset_write returns
// RV_ERR_INVAL there. A texture between the two limits therefore bakes and burns
// and is refused at run time.
constexpr int kMaxAxis = 65535;

// One decoded pixel of the source, already reduced to console precision, plus
// the verdict on whether it is a hole.
struct src_pixel {
    rv_color5 color;
    bool transparent = false;
};

// --- median cut ---------------------------------------------------------------

// A channel of rv_color5 is five bits, so 0..31 (pdk/cv/rv_texel.hpp).
constexpr int kChannel5Max = 31;

// Every colour the console can express: three channels of five bits. 32768
// counters is small enough to histogram by direct indexing, which is why this
// tool needs no hash map and has no ordering ambiguity to resolve.
constexpr size_t kColor5Codes = 1u << 15;

// Luma coefficients of ITU-R BT.601 (Y' = 0.299 R' + 0.587 G' + 0.114 B') taken
// ×10 and rounded: 2.99 -> 3, 5.87 -> 6, 1.14 -> 1. They weight the colour
// metric below so that palette accuracy is spent on green, which the eye
// resolves far better than blue. Integers keep every comparison exact and
// therefore reproducible across compilers.
constexpr int kLumaWeightR = 3;
constexpr int kLumaWeightG = 6;
constexpr int kLumaWeightB = 1;

// How many Lloyd passes run after median cut. A fixed count, not "until
// convergence": the gain collapses after the first few passes, and a fixed
// number keeps the tool terminating in bounded time with identical output run
// after run.
constexpr int kLloydPasses = 4;

struct color_bin {
    rv_color5 color;
    uint32_t count = 0; // how many source pixels carry this exact colour
};

// A box is a half-open SLICE of the bin array, not a geometric volume. It
// behaves like an axis-aligned box only because the slice is sorted along the
// splitting channel before every cut, which keeps both halves contiguous.
struct box {
    size_t begin = 0; // half-open range over the bin array
    size_t end = 0;
};

// The three channels as a closed set. An int would let any number through and
// channel_of would answer with blue for all of them, silently.
enum color_axis {
    COLOR_AXIS_R,
    COLOR_AXIS_G,
    COLOR_AXIS_B,
};

constexpr color_axis kColorAxes[] = { COLOR_AXIS_R, COLOR_AXIS_G, COLOR_AXIS_B };

uint8_t channel_of(const rv_color5 &c, color_axis axis)
{
    switch (axis) {
    case COLOR_AXIS_R:
        return c.r;
    case COLOR_AXIS_G:
        return c.g;
    case COLOR_AXIS_B:
        break;
    }
    return c.b; // the enum has no fourth value; this is the B arm
}

// What widest_axis answers. A pair would leave every caller remembering which of
// .first and .second is the channel and which is the length.
struct axis_span {
    color_axis axis = COLOR_AXIS_R;
    int span = 0;
};

// Longest edge of a box and the channel it runs along. The span is the crude
// measure of how badly one colour can stand in for the whole box: colours
// differing by 2 are already described by their mean, colours spanning 25 are
// not. Measured UNWEIGHTED — kLumaWeight* applies to distance2, not here.
axis_span widest_axis(const std::vector<color_bin> &bins, const box &b)
{
    axis_span best{ COLOR_AXIS_R, -1 };
    for (const color_axis axis : kColorAxes) {
        // Seeded at the ends of the channel range, so the first colour takes both.
        int lo = kChannel5Max;
        int hi = 0;
        for (size_t i = b.begin; i < b.end; ++i) {
            const int v = channel_of(bins[i].color, axis);
            lo = std::min(lo, v);
            hi = std::max(hi, v);
        }
        const int span = hi - lo;
        if (span > best.span) {
            best.span = span;
            best.axis = axis;
        }
    }
    return best;
}

// The one colour that stands in for a whole box: the mean of its colours weighted
// by pixel count, so a colour covering half the box pulls the entry half the way.
//
// RV_ERR_INVAL means the box holds no pixels, which is an INVARIANT VIOLATION
// rather than an expected outcome: a bin is only created for a colour with a
// non-zero count, and a split never produces an empty half (see the clamp in
// median_cut). The guard exists so a broken invariant cannot divide by zero.
rv_err gen_color5(const box &b, const std::vector<color_bin> &bins, rv_color5 *out)
{
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
        return RV_ERR_INVAL;
    }

    // + weight/2 rounds to nearest; integer division alone always truncates down.
    *out = rv_color5{ static_cast<uint8_t>((sr + weight / 2) / weight),
        static_cast<uint8_t>((sg + weight / 2) / weight),
        static_cast<uint8_t>((sb + weight / 2) / weight) };

    return RV_OK;
}

// Chooses the box to split next: longest edge wins, ties broken by pixel
// population so a large flat area is refined before a stray gradient. A box
// holding one colour cannot be split and is skipped.
//
// A SEARCH, not a fallible operation: false means every box is already a single
// colour, which is the normal answer for an image with fewer colours than the
// palette has slots — nothing failed. `target` is untouched in that case.
bool pick_box(const std::vector<box> &bs, const std::vector<color_bin> &bins, size_t *target)
{
    size_t best = bs.size();
    int best_span = 0;
    uint64_t best_pop = 0;
    for (size_t i = 0; i < bs.size(); ++i) {
        if (bs[i].end - bs[i].begin < 2) {
            continue; // a single colour cannot be split further
        }
        const int span = widest_axis(bins, bs[i]).span;
        uint64_t pop = 0;
        for (size_t k = bs[i].begin; k < bs[i].end; ++k) {
            pop += bins[k].count;
        }
        if (span > best_span || (span == best_span && pop > best_pop)) {
            best = i;
            best_span = span;
            best_pop = pop;
        }
    }
    if (best == bs.size() || best_span == 0) {
        return false;
    }

    *target = best;
    return true;
}

// Median cut — Heckbert, "Color Image Quantization for Frame Buffer Display",
// SIGGRAPH '82 (doi:10.1145/965145.801294). The palette is built by repeatedly
// halving the colour set instead of searching for optimal centroids.
//
// One box starts out holding every distinct opaque colour. Until there are as
// many boxes as palette slots, the worst box is sorted along its widest channel
// and split at the median BY PIXEL COUNT; each surviving box then contributes
// one entry, the weighted mean of its colours.
//
// Chosen over plain k-means because it is deterministic — the same PNG always
// bakes byte-identical output, which is what makes a build cache and a diff of
// committed assets mean anything — and because it cannot waste a slot on an
// empty cluster the way a random seed can. Splitting by population rather than
// by colour count is what suits flat-shaded artwork: a colour covering half the
// image gets half the palette even from a visually tiny corner of the cube.
//
// Its weakness is that boxes are axis-aligned, so a colour ramp lying diagonally
// in the cube is cut clumsily; refine() below repairs that. pngquant's
// libimagequant pairs the two algorithms the same way.
std::vector<rv_color5> median_cut(std::vector<color_bin> bins, size_t want)
{
    std::vector<rv_color5> palette;
    if (bins.empty() || want == 0) {
        return palette;
    }

    std::vector<box> boxes{ box{ 0, bins.size() } };
    while (boxes.size() < want) {
        size_t target = 0;
        if (!pick_box(boxes, bins, &target)) {
            break; // the image has fewer distinct colours than there are slots
        }

        box &b = boxes[target];
        const color_axis axis = widest_axis(bins, b).axis;
        std::sort(bins.begin() + static_cast<ptrdiff_t>(b.begin),
            bins.begin() + static_cast<ptrdiff_t>(b.end),
            [axis](const color_bin &x, const color_bin &y) {
                return channel_of(x.color, axis) < channel_of(y.color, axis);
            });

        // Split where the cumulative PIXEL COUNT crosses half, not the colour count.
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

        const box right{ split, b.end };
        b.end = split;
        boxes.push_back(right);
    }

    for (const box &b : boxes) {
        rv_color5 color5;
        if (gen_color5(b, bins, &color5) != RV_OK) {
            continue;
        }
        palette.push_back(color5);
    }
    return palette;
}

// SQUARED distance between two colours under the kLumaWeight* metric. Squared
// because every caller only compares results and sqrt is monotone: the ordering
// survives, while the arithmetic stays integer and therefore identical on every
// compiler — two near-equal candidates can never swap places on someone else's
// machine and bake a different file.
//
// The weights make this deliberately non-Euclidean: it is the squared length of
// the difference vector measured with the axes rescaled by sqrt(3), sqrt(6), 1.
// An error in blue is thus tolerated sqrt(6) ~ 2.45 times further than the same
// error in green. A finer metric that varies the weights with the red level is
// Riemersma's (https://www.compuphase.com/cmetric.htm); it is not used here.
uint32_t distance2(const rv_color5 &a, const rv_color5 &b)
{
    const int dr = static_cast<int>(a.r) - static_cast<int>(b.r);
    const int dg = static_cast<int>(a.g) - static_cast<int>(b.g);
    const int db = static_cast<int>(a.b) - static_cast<int>(b.b);
    return static_cast<uint32_t>(
        kLumaWeightR * dr * dr + kLumaWeightG * dg * dg + kLumaWeightB * db * db);
}

// Index of the palette entry closest to c. A linear scan is enough: the palette
// is at most 256 entries and this runs per DISTINCT colour, not per pixel. It
// only reads the palette — the entries themselves are moved by refine().
size_t nearest(const std::vector<rv_color5> &palette, const rv_color5 &c)
{
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

// Lloyd's algorithm — S. P. Lloyd, "Least Squares Quantization in PCM", Bell
// Labs 1957, published in IEEE Trans. Inf. Theory 28(2):129-137, 1982
// (doi:10.1109/TIT.1982.1056489); also known as Voronoi iteration, and as
// k-means when the initial centres are random. Here it relaxes the palette that
// median cut produced, repairing the ramps its axis-aligned boxes cut badly.
//
// One pass has two halves that must not be interleaved: assign every colour to
// its nearest entry, THEN move each entry to the weighted mean of what it
// received. The assignment reads the palette frozen at the start of the pass, so
// the result cannot depend on the order of `bins`.
//
// A pass cannot increase total error, and the weighted mean really is the
// minimiser under this metric: the weights are a constant factor in the
// derivative and cancel, which is why the mean needs no knowledge of
// kLumaWeight*. Passes are counted, not run to convergence — see kLloydPasses.
void refine(const std::vector<color_bin> &bins, std::vector<rv_color5> &palette, int passes)
{
    if (palette.empty()) {
        return;
    }
    for (int pass = 0; pass < passes; ++pass) {
        std::vector<uint64_t> weight(palette.size(), 0);
        std::vector<uint64_t> sr(palette.size(), 0);
        std::vector<uint64_t> sg(palette.size(), 0);
        std::vector<uint64_t> sb(palette.size(), 0);
        for (const color_bin &bin : bins) {
            const size_t k = nearest(palette, bin.color);
            weight[k] += bin.count;
            sr[k] += static_cast<uint64_t>(bin.color.r) * bin.count;
            sg[k] += static_cast<uint64_t>(bin.color.g) * bin.count;
            sb[k] += static_cast<uint64_t>(bin.color.b) * bin.count;
        }
        for (size_t i = 0; i < palette.size(); ++i) {
            if (weight[i] == 0) {
                continue; // an orphaned entry is left where it is, not moved
            }
            // + weight/2 rounds to nearest; plain integer division truncates down.
            palette[i] = rv_color5{ static_cast<uint8_t>((sr[i] + weight[i] / 2) / weight[i]),
                static_cast<uint8_t>((sg[i] + weight[i] / 2) / weight[i]),
                static_cast<uint8_t>((sb[i] + weight[i] / 2) / weight[i]) };
        }
    }
}

// --- command line -------------------------------------------------------------

// What the user asked for. Both optionals are absent until the flag that fills
// them appears, so "was not given" cannot be mistaken for a value — there is no
// spare `int` state and no companion bool to keep in step.
struct options {
    std::string input;
    std::string output;
    std::optional<rv_texfmt> format;
    std::optional<rv_color> key; // the colour that becomes a hole
    bool help = false;
};

// What a failed step hands back to the CLI. `show_usage` is set by argument
// parsing only; an empty message means the failure has already explained itself
// on stderr, which is how getopt reports an unknown flag.
struct baker_error {
    std::string message;
    bool show_usage = false;
};

// glibc keeps the invoked name here, so a renamed binary still prints the name
// the user typed. Elsewhere there is no such variable and the built-in spelling
// is used instead, which goes stale if the binary is renamed.
inline const char *rv_baker_progname()
{
#ifdef __GLIBC__
    return program_invocation_short_name;
#else
    return "mppcbaker";
#endif
}

// Every spelling the command line accepts, joined into one string. The
// separator goes BETWEEN entries and never after the last, so nothing has to be
// trimmed off the end and the caller picks the shape: ", " reads as prose, "|"
// is the alternation the usage line wants.
std::string format_texfmt_names(const char *separator)
{
    std::string res;
    bool first = true;
    for (const rv_pdklib::rv_texfmt_name &row : rv_pdklib::rv_texfmt_names) {
        if (!first) {
            res += separator;
        }
        res += row.text;
        first = false;
    }
    return res;
}

void rv_baker_print_usage(std::FILE *out)
{
    rv_pdklib::rv_fprintf(out,
        "usage: mppcbaker <input.png> <output.mppctex> --format %s\n"
        "                [--transparent-key RRGGBB]\n"
        "\n"
        "  --format            texel encoding (rv_texfmt): 4-bit or 8-bit palette\n"
        "                      index, or 15-bit direct colour.\n"
        "  --transparent-key   source colour (hex, e.g. FF00FF) to encode as the\n"
        "                      fully transparent value 0000h. PNG alpha < 128 is\n"
        "                      treated as transparent as well, always.\n",
        format_texfmt_names("|").c_str());
}

// The ONE place a failure becomes something the user sees and a code the shell
// gets. No function below this one ends the process or writes to stderr about a
// failure, which is what makes them callable more than once per run.
int report(const baker_error &error)
{
    if (error.show_usage) {
        rv_baker_print_usage(stderr);
    }
    if (!error.message.empty()) {
        rv_pdklib::rv_fprintf(stderr, "%s: %s\n", rv_baker_progname(), error.message.c_str());
    }
    return error.message.empty() ? kExitUsage : kExitFailure;
}

// Digits in an RRGGBB argument. Fixed rather than lenient: three-digit CSS
// shorthand and an alpha suffix would both parse into something plausible and
// wrong, and a mistyped key silently punches holes in the wrong colour.
constexpr size_t kHexRgbDigits = 6;

// Parses RRGGBB, with an optional leading '#', into an 8-bit colour.
bool parse_hex_rgb(std::string_view text, rv_color *out)
{
    if (!text.empty() && text.front() == '#') {
        text.remove_prefix(1);
    }
    if (text.size() != kHexRgbDigits) {
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
        value = (value << kNibbleBits) | static_cast<uint32_t>(digit);
    }
    out->r = static_cast<uint8_t>((value >> 16) & 0xFF);
    out->g = static_cast<uint8_t>((value >> 8) & 0xFF);
    out->b = static_cast<uint8_t>(value & 0xFF);
    return true;
}

// Fills `out` from argv. RV_ERR_INVAL is a command line that cannot be obeyed;
// --help is not one of those, it fills out->help and succeeds.
rv_err parse_args(int argc, char **argv, options *out, baker_error *error)
{
    static struct option long_opts[] = { { "help", no_argument, 0, 'h' },
        { "format", required_argument, 0, 'f' },
        { "transparent-key", required_argument, 0, 'k' },
        { 0, 0, 0, 0 } };

    int c;
    while ((c = getopt_long(argc, argv, "hf:k:", long_opts, nullptr)) != -1) {
        switch (c) {
        case 'h':
            out->help = true;
            return RV_OK; // nothing after --help is worth validating
        case 'f': {
            const rv_pdklib::rv_texfmt_name *texfmt_name_row = rv_pdklib::rv_texfmt_name::by_text(optarg);
            if (texfmt_name_row == nullptr) {
                error->message = "unknown format '" + std::string(optarg) + "'; expected " + format_texfmt_names(", ");
                return RV_ERR_INVAL;
            }
            out->format = texfmt_name_row->format;
            break;
        }
        case 'k': {
            rv_color key;
            if (!parse_hex_rgb(optarg, &key)) {
                error->message = "'" + std::string(optarg) + "' is not six hex digits (e.g. FF00FF)";
                return RV_ERR_INVAL;
            }
            out->key = key;
            break;
        }
        case '?':
            // getopt has already named the offending option on stderr.
            error->show_usage = true;
            return RV_ERR_INVAL;
        }
    }

    // optind is where getopt_long left the first non-flag argument: input, output.
    if (argc - optind != 2) {
        error->message = "expected exactly one input and one output path";
        error->show_usage = true;
        return RV_ERR_INVAL;
    }
    if (!out->format.has_value()) {
        error->message = "--format is required (" + format_texfmt_names(", ") + ")";
        return RV_ERR_INVAL;
    }
    out->input = argv[optind];
    out->output = argv[optind + 1];
    return RV_OK;
}

// --- source image -------------------------------------------------------------

// stb hands back a malloc-like pointer. This deleter is the only place in the
// program that knows that; every other line sees an owning C++ value that frees
// itself on any exit path.
struct stbi_deleter {
    void operator()(stbi_uc *pixels) const
    {
        stbi_image_free(pixels);
    }
};
using stbi_pixels = std::unique_ptr<stbi_uc, stbi_deleter>;

// The whole source, reduced once to console precision. Nothing downstream reads
// bytes from stb or asks how many channels the PNG had.
struct source_image {
    std::vector<src_pixel> pixels;
    int width = 0;
    int height = 0;
    size_t transparent_count = 0;
};

// Decodes the PNG named by the options and reduces it. RV_ERR_IO is a file that
// could not be read, RV_ERR_INVAL a file this container cannot describe.
rv_err load_source(const options &opt, source_image *out, baker_error *error)
{
    int width = 0;
    int height = 0;
    int source_channels = 0;
    const stbi_pixels pixels(
        stbi_load(opt.input.c_str(), &width, &height, &source_channels, kSourceChannels));
    if (pixels == nullptr) {
        const char *reason = stbi_failure_reason();
        error->message = "cannot read '" + opt.input + "': " + (reason != nullptr ? reason : "unknown");
        return RV_ERR_IO;
    }
    if (width <= 0 || height <= 0) {
        error->message = "'" + opt.input + "' has a zero dimension";
        return RV_ERR_INVAL;
    }
    // Refuse here rather than write a file the container cannot describe.
    if (width > kMaxAxis || height > kMaxAxis) {
        error->message = "'" + opt.input + "' is larger than " + std::to_string(kMaxAxis) + " texels on an axis";
        return RV_ERR_INVAL;
    }

    const size_t texel_count = static_cast<size_t>(width) * static_cast<size_t>(height);
    out->width = width;
    out->height = height;
    out->pixels.assign(texel_count, src_pixel{});
    for (size_t i = 0; i < texel_count; ++i) {
        const stbi_uc *p = pixels.get() + i * kSourceChannels;
        const rv_color c{ p[0], p[1], p[2] };
        const bool by_alpha = p[3] < kAlphaTransparentBelow;
        const bool by_key = opt.key.has_value() && c.r == opt.key->r && c.g == opt.key->g && c.b == opt.key->b;
        out->pixels[i].transparent = by_alpha || by_key;
        out->pixels[i].color = rv_texel_quantize(c);
        if (out->pixels[i].transparent) {
            ++out->transparent_count;
        }
    }
    return RV_OK;
}

// --- output -------------------------------------------------------------------

// Appends one little-endian uint16, the only multi-byte shape the format uses.
void put_u16(std::vector<uint8_t> &out, uint16_t v)
{
    out.push_back(static_cast<uint8_t>(v & 0xFF));
    out.push_back(static_cast<uint8_t>(v >> 8));
}

// The 16 fixed bytes every .mppctex opens with. RV_ERR_INVAL here is an
// invariant violation, not a user error: the layout is documented at kHeaderSize
// and a mismatch means this function and that constant have drifted apart.
rv_err write_header(rv_texfmt format, const source_image &src, uint16_t palette_entries,
    std::vector<uint8_t> *out, baker_error *error)
{
    out->insert(out->end(), kMagic, kMagic + sizeof(kMagic));
    put_u16(*out, kVersion);
    put_u16(*out, static_cast<uint16_t>(format));
    put_u16(*out, static_cast<uint16_t>(src.width));
    put_u16(*out, static_cast<uint16_t>(src.height));
    put_u16(*out, palette_entries);
    put_u16(*out, 0); // reserved
    if (out->size() != kHeaderSize) {
        error->message = "internal: header size drifted from the documented layout";
        return RV_ERR_INVAL;
    }
    return RV_OK;
}

// DIRECT15 carries no palette: a texel is the colour itself.
void encode_direct15(const source_image &src, std::vector<uint8_t> *out)
{
    out->reserve(out->size() + src.pixels.size() * 2);
    for (const src_pixel &s : src.pixels) {
        put_u16(*out,
            s.transparent ? rv_pdk::RV_TEXEL_TRANSPARENT : rv_texel_opaque(rv_texel_pack(s.color)));
    }
}

// One bin per distinct OPAQUE colour, with the pixel count that colour carries.
// A hole keeps whatever RGB the artist left under the alpha, and that colour
// must not drag the palette towards it.
void histogram(const source_image &src, std::vector<color_bin> *out)
{
    // Bucketed directly, one counter per code — see kColor5Codes.
    std::vector<uint32_t> counts(kColor5Codes, 0);
    for (const src_pixel &s : src.pixels) {
        if (!s.transparent) {
            ++counts[rv_texel_pack(s.color)];
        }
    }
    for (uint32_t code = 0; code < counts.size(); ++code) {
        if (counts[code] != 0) {
            out->push_back(color_bin{ rv_texel_unpack(static_cast<uint16_t>(code)), counts[code] });
        }
    }
}

// IDX4 rows, two texels to a byte — see kNibbleBits.
void pack_nibbles(const source_image &src, const std::vector<uint8_t> &indices,
    std::vector<uint8_t> *out)
{
    const size_t width = static_cast<size_t>(src.width);
    // The +1 is what pads an odd width so every row still starts on a byte.
    const size_t stride = (width + 1) / 2;
    out->reserve(out->size() + stride * static_cast<size_t>(src.height));
    for (int y = 0; y < src.height; ++y) {
        const size_t row = static_cast<size_t>(y) * width;
        for (size_t x = 0; x < width; x += 2) {
            const uint8_t low = static_cast<uint8_t>(indices[row + x] & kNibbleMask);
            const uint8_t high = (x + 1 < width) ? static_cast<uint8_t>(indices[row + x + 1] & kNibbleMask) : uint8_t{ 0 };
            out->push_back(static_cast<uint8_t>(low | (high << kNibbleBits)));
        }
    }
}

// Header, palette and indices for IDX4/IDX8. RV_ERR_INVAL is an image with
// nothing to put in a palette.
rv_err encode_indexed(const options &opt, const source_image &src, std::vector<uint8_t> *out,
    baker_error *error)
{
    const size_t palette_size = (*opt.format == RV_TEXFMT_IDX4) ? kPaletteSizeIdx4 : kPaletteSizeIdx8;
    // One slot spent on kHoleIndex, and only when the image needs a hole.
    const bool needs_hole = src.transparent_count > 0;
    const size_t reserved = needs_hole ? 1u : 0u;
    const size_t color_slots = palette_size - reserved;

    std::vector<color_bin> bins;
    histogram(src, &bins);
    if (bins.empty()) {
        error->message = "'" + opt.input + "' has no opaque pixel: there is nothing to put in a palette";
        return RV_ERR_INVAL;
    }
    if (bins.size() > color_slots) {
        rv_pdklib::rv_fprintf(stderr,
            "mppcbaker: note: %zu distinct colours reduced to %zu palette entries\n", bins.size(),
            color_slots);
    }

    std::vector<rv_color5> palette = median_cut(bins, color_slots);
    refine(bins, palette, kLloydPasses);

    // After refinement, so the indices match the palette actually written.
    std::vector<uint8_t> indices(src.pixels.size(), kHoleIndex);
    for (size_t i = 0; i < src.pixels.size(); ++i) {
        if (src.pixels[i].transparent) {
            continue; // already kHoleIndex; `reserved` is 1 whenever this is reached
        }
        indices[i] = static_cast<uint8_t>(nearest(palette, src.pixels[i].color) + reserved);
    }

    const rv_err header = write_header(*opt.format, src, static_cast<uint16_t>(palette_size), out, error);
    if (header != RV_OK) {
        return header;
    }

    // Full length always; the unused tail is 0000h, so an index that should
    // never be sampled draws nothing rather than a wrong colour.
    if (needs_hole) {
        put_u16(*out, rv_pdk::RV_TEXEL_TRANSPARENT);
    }
    for (const rv_color5 &c : palette) {
        put_u16(*out, rv_texel_opaque(rv_texel_pack(c)));
    }
    for (size_t i = reserved + palette.size(); i < palette_size; ++i) {
        put_u16(*out, rv_pdk::RV_TEXEL_TRANSPARENT);
    }

    if (*opt.format == RV_TEXFMT_IDX8) {
        out->insert(out->end(), indices.begin(), indices.end());
    } else {
        pack_nibbles(src, indices, out);
    }
    return RV_OK;
}

// The whole .mppctex, in memory. The format decides which encoder runs; both
// produce a complete file, header included.
rv_err encode_texture(const options &opt, const source_image &src, std::vector<uint8_t> *out,
    baker_error *error)
{
    if (*opt.format == RV_TEXFMT_DIRECT15) {
        const rv_err header = write_header(*opt.format, src, 0, out, error);
        if (header != RV_OK) {
            return header;
        }
        encode_direct15(src, out);
        return RV_OK;
    }
    return encode_indexed(opt, src, out, error);
}

// One open, one write, one close. Everything the caller can do about a failure
// here is report it, so the message says what was being attempted.
rv_err write_file(const std::string &path, const std::vector<uint8_t> &bytes, baker_error *error)
{
    std::FILE *out = std::fopen(path.c_str(), "wb");
    if (out == nullptr) {
        error->message = "cannot open '" + path + "' for writing";
        return RV_ERR_IO;
    }
    const size_t written = std::fwrite(bytes.data(), 1, bytes.size(), out);
    const bool closed = std::fclose(out) == 0;
    if (written != bytes.size() || !closed) {
        error->message = "failed to write '" + path + "' (disk full?)";
        return RV_ERR_IO;
    }
    return RV_OK;
}

// --- the tool -------------------------------------------------------------------

// The program, one step to a line: read the command line, load the source, bake
// the texture, write it, say what happened. This is the CLI boundary — the only
// function that turns a report into an exit code. main() below holds no logic of
// its own: it has to live in the GLOBAL namespace because the language says so,
// and that is the only reason it is outside rv_pdktools.
int run(int argc, char **argv)
{
    options opt;
    baker_error error;

    if (parse_args(argc, argv, &opt, &error) != RV_OK) {
        return report(error);
    }
    if (opt.help) {
        rv_baker_print_usage(stdout);
        return kExitSuccess;
    }

    source_image src;
    if (load_source(opt, &src, &error) != RV_OK) {
        return report(error);
    }

    std::vector<uint8_t> file;
    if (encode_texture(opt, src, &file, &error) != RV_OK) {
        return report(error);
    }
    if (write_file(opt.output, file, &error) != RV_OK) {
        return report(error);
    }

    rv_pdklib::rv_texfmt_name::by_format(*opt.format);
    std::printf("mppcbaker: %s -> %s (%dx%d, %s, %zu bytes)\n",
        opt.input.c_str(),
        opt.output.c_str(),
        src.width,
        src.height,
        rv_pdklib::rv_texfmt_name::by_format(*opt.format)->text,
        file.size());
    return kExitSuccess;
}

} // namespace
} // namespace rv_pdktools

int main(int argc, char **argv)
{
    return rv_pdktools::run(argc, argv);
}
