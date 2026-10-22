// mppcmodbaker: bakes a glTF 2.0 model into a .mppcmod and the PNG textures it embeds.
#include <getopt.h>

#include <cstdint>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "pdk/de/rv_dv.h"
#include "pdk/rv_err.h"
#include "pdklib/rv_models/rv_mppcmod.hpp"
#include "pdklib/rv_models/rv_mppcmod_gltf.hpp"
#include "pdklib/rv_models/rv_mppcmod_int.hpp"
#include "pdklib/rv_models/rv_mppcmod_quant.hpp"
#include "pdklib/rv_stdio/rv_stdio.hpp"

namespace
{

// Exit codes: 2 is an unknown option (getopt's '?'); 1 is any other failure, including a
// wrong number of arguments.
constexpr int RV_MODBAKER_EXIT_SUCCESS = 0;
constexpr int RV_MODBAKER_EXIT_FAILURE = 1;
constexpr int RV_MODBAKER_EXIT_USAGE = 2;

// Prefix of every message. Fixed rather than the invoked name, so output does not
// depend on the name the binary was copied under.
constexpr const char *RV_MODBAKER_PROGNAME = "mppcmodbaker";

// Argc for `mppcmodbaker --version`: program name plus the one flag.
constexpr int RV_MODBAKER_ARGC_VERSION_CHECK = 2;

// Positional arguments required after option processing: input and output paths.
constexpr int RV_MODBAKER_POSITIONAL_ARGS_REQUIRED = 2;

// The extension a baked model carries; the texture names come from the rest of the name.
constexpr std::string_view RV_MODBAKER_MODEL_EXT = ".mppcmod";
// The PNGs sit beside the output as <stem>-<n>.png.
constexpr std::string_view RV_MODBAKER_PNG_EXT = ".png";

// What a failed step hands back to the CLI. `show_usage` is set by argument
// parsing only; an empty message means the failure has already explained itself
// on stderr, which is how getopt reports an unknown flag.
struct modbaker_error {
    std::string message;
    bool show_usage = false;
};

struct modbaker_args {
    std::string input;
    std::string output;
    bool help = false;
};

void print_usage(std::FILE *out)
{
    rv_pdklib::rv_fprintf(out,
        "usage: mppcmodbaker <input.glb|input.gltf> <output.mppcmod>\n"
        "\n"
        "  A glTF 2.0 model is baked to a .mppcmod: a .glb, or a .gltf whose .bin and PNG files\n"
        "  are named by relative URIs ('..' allowed). Each PNG texture is written beside the\n"
        "  output as <stem>-<n>.png; the parts name them <stem>-<n>.mppctex. JPEG textures are refused.\n"
        "\n"
        "mppcmodbaker --version prints one line, `mppcmodbaker <major>.<minor>`.\n");
}

// The ONE place a failure becomes something the user sees and a code the shell
// gets. An empty message means the failure has already explained itself.
int report(const modbaker_error &error)
{
    if (error.show_usage) {
        print_usage(stderr);
    }
    if (!error.message.empty()) {
        rv_pdklib::rv_fprintf(stderr, "%s: %s\n", RV_MODBAKER_PROGNAME, error.message.c_str());
    }
    return error.message.empty() ? RV_MODBAKER_EXIT_USAGE : RV_MODBAKER_EXIT_FAILURE;
}

// Fills `out` from argv. --help is not a failure: it sets out->help and succeeds.
int parse_args(int argc, char **argv, modbaker_args *out, modbaker_error *error)
{
    static struct option long_opts[] = { { "help", no_argument, 0, 'h' }, { 0, 0, 0, 0 } };

    int c;
    while ((c = getopt_long(argc, argv, "h", long_opts, nullptr)) != -1) {
        switch (c) {
        case 'h':
            out->help = true;
            return RV_OK;
        case '?':
            // getopt has already named the offending option on stderr.
            error->show_usage = true;
            return RV_ERR_INVAL;
        }
    }

    if (argc - optind != RV_MODBAKER_POSITIONAL_ARGS_REQUIRED) {
        error->message = "expected exactly one input and one output path";
        error->show_usage = true;
        return RV_ERR_INVAL;
    }
    out->input = argv[optind];
    out->output = argv[optind + 1];
    return RV_OK;
}

// One open, one write, one close. The message says what was being attempted.
int write_file(const std::string &path, const std::vector<uint8_t> &bytes, modbaker_error &error)
{
    std::FILE *out = std::fopen(path.c_str(), "wb");
    if (out == nullptr) {
        error.message = "cannot open '" + path + "' for writing";
        return RV_ERR_IO;
    }
    const size_t written = std::fwrite(bytes.data(), 1, bytes.size(), out);
    const bool closed = std::fclose(out) == 0;
    if (written != bytes.size() || !closed) {
        error.message = "failed to write '" + path + "' (disk full?)";
        return RV_ERR_IO;
    }
    return RV_OK;
}

// The output's file name without .mppcmod: the stem the texture names are built from.
std::string output_stem(const std::string &output)
{
    std::string name = std::filesystem::path(output).filename().string();
    if (std::string_view(name).ends_with(RV_MODBAKER_MODEL_EXT)) {
        name.resize(name.size() - RV_MODBAKER_MODEL_EXT.size());
    }
    return name;
}

// Each embedded PNG beside the output: <output directory>/<name>.png.
int write_pngs(const std::string &output, const std::vector<rv_pdklib::rv_mppcmod_gltf_image> &images, modbaker_error &error)
{
    const std::filesystem::path dir = std::filesystem::path(output).parent_path();
    for (const rv_pdklib::rv_mppcmod_gltf_image &image : images) {
        const std::string path = (dir / (image.name + std::string(RV_MODBAKER_PNG_EXT))).string();
        const int rc = write_file(path, image.png, error);
        if (rc != RV_OK) {
            return rc;
        }
    }
    return RV_OK;
}

int run(int argc, char **argv)
{
    modbaker_args args;
    modbaker_error error;

    if (parse_args(argc, argv, &args, &error) != RV_OK) {
        return report(error);
    }
    if (args.help) {
        print_usage(stdout);
        return RV_MODBAKER_EXIT_SUCCESS;
    }

    const std::string stem = output_stem(args.output);
    if (stem.empty()) {
        error.message = "output '" + args.output + "' has no name before .mppcmod to name its textures after";
        return report(error);
    }

    rv_pdklib::rv_mppcmod model;
    std::vector<rv_pdklib::rv_mppcmod_gltf_image> images;
    std::string bake_error;
    if (rv_pdklib::rv_mppcmod_read_gltf(args.input, stem, model, images, bake_error) != RV_OK) {
        error.message = bake_error;
        return report(error);
    }
    rv_pdklib::rv_mppcmod_int_model int_model;
    if (rv_pdklib::rv_mppcmod_quantize(model, int_model, bake_error) != RV_OK) {
        error.message = "'" + args.input + "' " + bake_error;
        return report(error);
    }
    std::vector<uint8_t> bytes;
    if (rv_pdklib::rv_mppcmod_int_write(int_model, &bytes, bake_error) != RV_OK) {
        error.message = "'" + args.input + "' " + bake_error;
        return report(error);
    }
    if (write_pngs(args.output, images, error) != RV_OK) {
        return report(error);
    }
    if (write_file(args.output, bytes, error) != RV_OK) {
        return report(error);
    }

    std::printf("%s: %s -> %s (%zu parts, %zu vertices, %zu triangles, %zu PNG, vertex shift %d)\n",
        RV_MODBAKER_PROGNAME,
        args.input.c_str(),
        args.output.c_str(),
        model.parts.size(),
        model.vertices.size(),
        model.triangles.size(),
        images.size(),
        static_cast<int>(int_model.shift));
    return RV_MODBAKER_EXIT_SUCCESS;
}

} // namespace

int main(int argc, char **argv)
{
    // One line a front end can show and compare.
    if (argc == RV_MODBAKER_ARGC_VERSION_CHECK && std::string_view(argv[1]) == "--version") {
        rv_pdklib::rv_fprintf(stdout, "%s %d.%d\n", RV_MODBAKER_PROGNAME, RV_MPPC_VER_MAJOR, RV_MPPC_VER_MINOR);
        return RV_MODBAKER_EXIT_SUCCESS;
    }
    // Safety net only: the reader refuses what would throw. A throw still ends as a message.
    try {
        return run(argc, argv);
    } catch (const std::exception &e) {
        rv_pdklib::rv_fprintf(stderr, "%s: %s\n", RV_MODBAKER_PROGNAME, e.what());
        return RV_MODBAKER_EXIT_FAILURE;
    }
}
