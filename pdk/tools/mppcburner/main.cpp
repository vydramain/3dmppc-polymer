// mppcburner - burn a disc directory into one .mppcdisc, or inspect a burned one.
//
// A prepared directory goes in (sources, disc.toml, artwork) and a single
// .mppcdisc comes out. That image is the only thing the console loads.
//
// Streams: inspect writes its listing to stdout and nothing else, so it pipes.
// build writes every progress line to stderr, because its product is a file,
// not a stream. Any refusal exits non-zero.
//
// The command line is table driven: OPTIONS and COMMANDS below are the only
// place a subcommand or an option is declared.

#include <climits>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <string_view>
#include <vector>

#include <getopt.h>

#include "pdk/de/rv_dv.h"
#include "pdklib/rv_stdio/rv_stdio.hpp"

#include "rv_burner_build/rv_burner_build_runner.hpp"
#include "rv_burner_options.hpp"
#include "rv_burner_print.hpp"
#include "rv_burner_inspect/rv_burner_inspect_runner.hpp"

namespace rv_pdktools
{
static void print_usage(std::FILE *stream)
{
    rv_pdklib::rv_fprintf(stream,
        "mppcburner - burn a disc directory into a .mppcdisc image\n"
        "\n"
        "Usage:\n"
        "  mppcburner build <disc-directory> -o <output.mppcdisc> [options]\n"
        "  mppcburner inspect <file.mppcdisc>\n"
        "  mppcburner bake-texture <disc-directory> <source> -o <output.mppctex> [--baker PATH]\n"
        "  mppcburner help\n"
        "\n"
        "Commands:\n"
        "  build         Compile the disc directory and burn the image. Progress\n"
        "                goes to stderr, nothing to stdout.\n"
        "  inspect       Print the manifest and the entry sizes to stdout. Nothing\n"
        "                is unpacked and nothing is written.\n"
        "  bake-texture  Bake one [textures] source of the disc into a standalone\n"
        "                .mppctex, the way build's own plan would. The baked\n"
        "                entry's archive name is the only thing on stdout.\n"
        "  help          Print this text.\n"
        "\n"
        "Options (build):\n"
        "  -o, --output PATH        Image to write. Required unless --unpacked is\n"
        "                           given; exclusive with it.\n"
        "  -u, --unpacked PATH      Write an unpacked disc directory at PATH instead\n"
        "                           of a .mppcdisc image. Scripts and copied assets\n"
        "                           are symlinked back to their source instead of\n"
        "                           being compiled/copied, so the console sees a live\n"
        "                           edit without a rebuild. Exclusive with -o.\n"
        "  -p, --pdk PATH           PDK include directory the disc compiles\n"
        "                           against. Default: " RV_BURNER_DEFAULT_PDK
        "\n"
        "  -l, --pdklib PATH        Disc-side SDK include directory.\n"
        "                           Default: " RV_BURNER_DEFAULT_PDKLIB
        "\n"
        "  -b, --baker PATH         mppcbaker used to bake textures. Default: the\n"
        "                           copy next to mppcburner, then $PATH.\n"
        "  -j, --jobs N             Parallel compile jobs. Default: cmake decides.\n"
        "  -k, --keep-build[=PATH]  Keep the generated CMake project instead of\n"
        "                           deleting it. PATH must be attached with '='.\n"
        "  -m, --map PATH           After a build that succeeded, write which entry\n"
        "                           of the disc each of its files became to PATH.\n"
        "\n"
        "Options (bake-texture):\n"
        "  -o, --output PATH        .mppctex file to write. Required.\n"
        "  -b, --baker PATH         mppcbaker used to bake the texture. Default: the\n"
        "                           copy next to mppcburner, then $PATH.\n"
        "\n"
        "Options (any command):\n"
        "  -h, --help               Print this text.\n"
        "\n"
        "  mppcburner --version prints one line, `mppcburner <major>.<minor>`: the\n"
        "  PDK version the discs it builds are stamped with.\n");
}

// bake-texture's own bit; not in rv_burner_options.hpp because no other file
// needs to test for it.
constexpr uint32_t RV_BURNER_MASK_BAKE = 1u << 2;

// Every option the tool has, declared once. Letters are unique tool-wide, which
// is what lets a single switch store the options of every subcommand.
constexpr rv_burner_option_spec OPTIONS[] = {
    { 'h', "help", no_argument, "print this text",
        RV_BURNER_MASK_BUILD | RV_BURNER_MASK_INSPECT | RV_BURNER_MASK_BAKE },
    { 'o', "output", required_argument, "image to write (required unless --unpacked)",
        RV_BURNER_MASK_BUILD | RV_BURNER_MASK_BAKE },
    { 'u', "unpacked", required_argument, "write an unpacked disc directory instead",
        RV_BURNER_MASK_BUILD },
    { 'p', "pdk", required_argument,
        "PDK include directory the disc compiles against",
        RV_BURNER_MASK_BUILD },
    { 'l', "pdklib", required_argument, "disc-side SDK include directory",
        RV_BURNER_MASK_BUILD },
    { 'b', "baker", required_argument, "mppcbaker used to bake textures",
        RV_BURNER_MASK_BUILD | RV_BURNER_MASK_BAKE },
    { 'j', "jobs", required_argument, "parallel compile jobs",
        RV_BURNER_MASK_BUILD },
    { 'k', "keep-build", optional_argument,
        "keep the generated CMake project, optionally at PATH",
        RV_BURNER_MASK_BUILD },
    { 'm', "map", required_argument, "write the source-to-entry map to PATH",
        RV_BURNER_MASK_BUILD },
};

static int nullable_handler(const rv_burner_options &)
{
    rv_pdktools::print_usage(stderr);
    return -1;
}

// Every subcommand, plus the three spellings of help. A new command is a row
// here, never a branch in main.
constexpr rv_burner_command_spec COMMANDS[] = {
    { "build", RV_BURNER_MASK_BUILD, 1, rv_burner_build_run },
    { "inspect", RV_BURNER_MASK_INSPECT, 1, rv_burner_inspect_run },
    { "bake-texture", RV_BURNER_MASK_BAKE, 2, rv_burner_bake_texture_run },
    { "help", 0, 0, nullable_handler },
    { "-h", 0, 0, nullable_handler },
    { "--help", 0, 0, nullable_handler },
};
} // namespace rv_pdktools

int main(int argc, char **argv)
{
    if (argc < 2) {
        rv_pdktools::print_usage(stderr);
        return 1;
    }

    const std::string_view command_name = argv[1];
    // One line a front end can show and compare (the editor's diagnostics).
    if (command_name == "--version") {
        rv_pdklib::rv_fprintf(stdout, "mppcburner %d.%d\n", RV_MPPC_VER_MAJOR, RV_MPPC_VER_MINOR);
        return 0;
    }

    const rv_pdktools::rv_burner_command_spec *cmd = nullptr;
    for (const rv_pdktools::rv_burner_command_spec &command_intern : rv_pdktools::COMMANDS) {
        if (command_intern.name == command_name) {
            cmd = &command_intern;
            break;
        }
    }

    if (nullptr == cmd) {
        rv_pdktools::rv_burner_print_error("unknown subcommand '" + std::string(command_name) + "'");
        rv_pdktools::print_usage(stderr);
        return 1;
    }

    std::vector<struct option> opts;
    std::string opts_string;

    for (const struct rv_pdktools::rv_burner_option_spec &option_intern : rv_pdktools::OPTIONS) {
        if (0 == (option_intern.mask & cmd->mask)) {
            continue;
        }

        opts.push_back(option{
            option_intern.name,
            option_intern.arity,
            0,
            option_intern.letter,
        });

        opts_string.push_back(option_intern.letter);

        if (required_argument == option_intern.arity) {
            opts_string.push_back(':');
        }
        if (optional_argument == option_intern.arity) {
            opts_string.append("::");
        }
    }
    opts.push_back(option{ 0, 0, 0, 0 });

    rv_pdktools::rv_burner_options burner_options{};

    // Input args starts from second array member. So we need to skip command name first to check options
    const int local_argc = argc - 1;
    char **local_argv = argv + 1;
    for (int c;;) {
        c = getopt_long(local_argc, local_argv, opts_string.c_str(), opts.data(), nullptr);

        if (c < 0) {
            break;
        }

        switch (c) {
        case 'h': {
            rv_pdktools::print_usage(stdout);
            return 0;
        }
        case 'o': {
            burner_options.output = optarg;
            break;
        }
        case 'u': {
            burner_options.unpacked = optarg;
            break;
        }
        case 'p': {
            burner_options.pdk_dir = optarg;
            break;
        }
        case 'l': {
            burner_options.pdklib_dir = optarg;
            break;
        }
        case 'b': {
            burner_options.baker = optarg;
            break;
        }
        case 'j': {
            char *end = nullptr;
            const long value = std::strtol(optarg, &end, 10);
            if (*optarg == '\0' || *end != '\0' || value <= 0 || value > INT_MAX) {
                rv_pdktools::rv_burner_print_error("invalid --jobs value '" + std::string(optarg) + "'");
                return 1;
            }
            burner_options.jobs = static_cast<int>(value);
            break;
        }
        case 'm': {
            burner_options.map = optarg;
            break;
        }
        case 'k': {
            burner_options.keep_build = true;

            if (nullptr != optarg) {
                burner_options.build_dir = optarg;
            }
            break;
        }
        case '?':
        default: {
            // No message here: getopt already printed one (opterr defaults to 1).
            rv_pdktools::print_usage(stderr);
            return 1;
        }
        }
    }

    // Exactly the declared number of operands: getopt left them from optind on.
    if (cmd->operands != local_argc - optind) {
        rv_pdktools::rv_burner_print_error(std::string(command_name) + " expects " + std::to_string(cmd->operands) + " operand(s), got " + std::to_string(local_argc - optind));
        rv_pdktools::print_usage(stderr);
        return 1;
    }

    // Nothing to read for an operandless command: argv[optind] is its nullptr.
    // A second operand (bake-texture only) is the source to bake.
    if (cmd->operands >= 1) {
        burner_options.operand = local_argv[optind];
    }
    if (cmd->operands >= 2) {
        burner_options.bake_source = local_argv[optind + 1];
    }

    if (0 != (cmd->mask & rv_pdktools::RV_BURNER_MASK_BAKE) && burner_options.output.empty()) {
        rv_pdktools::rv_burner_print_error("bake-texture requires -o <output.mppctex>");
        return 1;
    }

    return cmd->handler(burner_options);
}
