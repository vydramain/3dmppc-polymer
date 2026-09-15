#include <getopt.h>

#include <cctype>
#include <cstdint>
#include <cerrno>
#include <cstdio>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "pdk/cv/rv_texel.h"
#include "pdk/cv/rv_texture.h"
#include "pdk/cv/rv_vertex.h"
#include "pdk/rv_err.h"
#include "pdklib/rv_stdio/rv_stdio.hpp"
#include "pdklib/rv_textures/rv_texel_pack.hpp"
#include "pdklib/rv_textures/rv_texfmt_name.hpp"
#include "rv_baker_encode.hpp"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

// A HOST program, not part of the machine: rv_pdktools is where the author's
// tools live. What it takes from outside is everything the CONSOLE dictates and
// the baker must reproduce byte for byte - the colour types from pdk/, the texel
// layout and the 8-to-5 quantiser from pdklib/rv_textures. A second spelling of
// any of those is a bug waiting for the day someone changes one copy. What
// remains below describes the .mppctex CONTAINER, which no console ever opens,
// and is therefore stated here.
namespace rv_pdktools
{
namespace
{

// Exit codes, kept apart so a build script can tell a bad invocation from a bad
// asset: 1 is a job that could not be done, 2 a command line not understood.
// These belong to the process, so only the CLI boundary ever produces them.
constexpr int RV_BAKER_EXIT_SUCCESS = 0;
constexpr int RV_BAKER_EXIT_FAILURE = 1;
constexpr int RV_BAKER_EXIT_USAGE = 2;

using rv_pdklib::rv_texel_quantize;

// --- source image -------------------------------------------------------------

// stb_image is asked for this many channels whatever the PNG holds, so a source
// without alpha comes back fully opaque and every "does this file have alpha"
// branch disappears from the code below.
constexpr int RV_BAKER_SOURCE_CHANNELS = 4;

// The console does not blend: a texel is either drawn or it is a hole. Anything
// below half opacity becomes a hole. The exact cut is arbitrary - it only has to
// be fixed, so the same PNG always bakes the same way.
constexpr uint8_t RV_BAKER_ALPHA_TRANSPARENT_BELOW = 128;

// Width and height are stored as uint16 in the header, so nothing larger can be
// described by the container at all. This is the CONTAINER's limit, not the
// machine's: the console's own texture_max_width/height (256 on the reference
// machine) is per-disc configuration this tool never sees, and it is enforced
// only when the texture is uploaded - rv_pccv::video_asset_write returns
// RV_ERR_INVAL there. A texture between the two limits therefore bakes and burns
// and is refused at run time.
constexpr int RV_BAKER_MAX_AXIS = 65535;

// --- command line -------------------------------------------------------------

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
    return error.message.empty() ? RV_BAKER_EXIT_USAGE : RV_BAKER_EXIT_FAILURE;
}

// Digits in an RRGGBB argument. Fixed rather than lenient: three-digit CSS
// shorthand and an alpha suffix would both parse into something plausible and
// wrong, and a mistyped key silently punches holes in the wrong colour.
constexpr size_t RV_BAKER_HEX_RGB_DIGITS = 6;

// Bits in one hex digit of RRGGBB.
constexpr int RV_BAKER_HEX_DIGIT_BITS = 4;

// Parses RRGGBB, with an optional leading '#', into an 8-bit colour.
bool parse_hex_rgb(std::string_view text, rv_color *out)
{
    if (!text.empty() && text.front() == '#') {
        text.remove_prefix(1);
    }
    if (text.size() != RV_BAKER_HEX_RGB_DIGITS) {
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
        value = (value << RV_BAKER_HEX_DIGIT_BITS) | static_cast<uint32_t>(digit);
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

// Decodes the PNG named by the options and reduces it. RV_ERR_IO is a file that
// could not be read, RV_ERR_INVAL a file this container cannot describe.
rv_err load_source(const options &opt, source_image *out, baker_error *error)
{
    int width = 0;
    int height = 0;
    int source_channels = 0;
    const stbi_pixels pixels(
        stbi_load(opt.input.c_str(), &width, &height, &source_channels, RV_BAKER_SOURCE_CHANNELS));
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
    if (width > RV_BAKER_MAX_AXIS || height > RV_BAKER_MAX_AXIS) {
        error->message = "'" + opt.input + "' is larger than " + std::to_string(RV_BAKER_MAX_AXIS) + " texels on an axis";
        return RV_ERR_INVAL;
    }

    const size_t texel_count = static_cast<size_t>(width) * static_cast<size_t>(height);
    out->width = width;
    out->height = height;
    out->pixels.assign(texel_count, src_pixel{});
    for (size_t i = 0; i < texel_count; ++i) {
        const stbi_uc *p = pixels.get() + i * RV_BAKER_SOURCE_CHANNELS;
        const rv_color c{ p[0], p[1], p[2] };
        const bool by_alpha = p[3] < RV_BAKER_ALPHA_TRANSPARENT_BELOW;
        const bool by_key = opt.key.has_value() && c.r == opt.key->r && c.g == opt.key->g && c.b == opt.key->b;
        out->pixels[i].transparent = by_alpha || by_key;
        out->pixels[i].color = rv_texel_quantize(c);
        if (out->pixels[i].transparent) {
            ++out->transparent_count;
        }
    }
    return RV_OK;
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
// the texture, write it, say what happened. This is the CLI boundary - the only
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
        return RV_BAKER_EXIT_SUCCESS;
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
    return RV_BAKER_EXIT_SUCCESS;
}

} // namespace
} // namespace rv_pdktools

int main(int argc, char **argv)
{
    return rv_pdktools::run(argc, argv);
}
