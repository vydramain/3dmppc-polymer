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
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include <getopt.h>

#include "pdk/de/rv_dv.h"
#include "pdk/rv_err.h"
#include "pdklib/rv_stdio/rv_stdio.hpp"

#include "rv_burner_build/rv_burner_build_runner.hpp"
#include "rv_burner_common/rv_burner_process.hpp"
#include "rv_burner_options.hpp"
#include "rv_burner_print.hpp"
#include "rv_burner_inspect/rv_burner_inspect_runner.hpp"
#include "rv_burner_setup/rv_burner_setup.hpp"

namespace
{
// Commands with bake-texture need 2 operands: disc directory and source to bake.
constexpr int k_bake_texture_operands = 2;

// Minimum argc: program name plus command.
constexpr int k_min_argc = 2;

// Base 10 for decimal number parsing in strtol.
constexpr int k_decimal_base = 10;

// The copies tools/build puts beside mppcburner (see mppcburner_pdk in CMakeLists.txt).
constexpr const char *k_default_pdk_subdir = "pdk/include";
constexpr const char *k_default_pdklib_subdir = "pdk/lib/include";
} // namespace

namespace rv_pdktools
{
// One default include directory: the copy beside the binary, the option that replaces it, and
// the field it fills.
struct rv_default_dir {
    const char *subdir;
    const char *flag;
    std::string rv_burner_options::*field;
};

constexpr rv_default_dir k_default_dirs[] = {
    { k_default_pdk_subdir, "pdk", &rv_burner_options::pdk_dir },
    { k_default_pdklib_subdir, "pdklib", &rv_burner_options::pdklib_dir },
};

// A default directory must exist: a missing copy is a broken package, not a path to trust.
static int take_default_dir(const std::filesystem::path &dir, const char *flag, std::string &field)
{
    std::error_code ec;
    const bool is_dir = std::filesystem::is_directory(dir, ec);
    if (!is_dir) {
        const std::string reason = ec ? ec.message() : "not a directory";
        rv_burner_print_error("cannot use " + dir.string() + " beside mppcburner (" + reason + "); pass --" + flag + " PATH");
        return RV_ERR_NOENT;
    }

    field = dir.string();
    return RV_OK;
}

// Fills the include directories the command line did not name, from the copies beside
// this binary. A path the user gave is used as it is.
static int resolve_pdk_dirs(rv_burner_options &options)
{
    const std::filesystem::path bin_dir = executable_directory();
    for (const rv_default_dir &entry : k_default_dirs) {
        if (!(options.*entry.field).empty()) {
            continue;
        }
        if (bin_dir.empty()) {
            rv_burner_print_error(std::string("cannot find the directory of mppcburner; pass --") + entry.flag + " PATH");
            return RV_ERR_NOENT;
        }

        const int rc = take_default_dir(bin_dir / entry.subdir, entry.flag, options.*entry.field);
        if (RV_OK != rc) {
            return rc;
        }
    }
    return RV_OK;
}

static void print_usage(std::FILE *stream)
{
    rv_pdklib::rv_fprintf(stream,
        "mppcburner - burn a disc directory into a .mppcdisc image\n"
        "\n"
        "Usage:\n"
        "  mppcburner build <disc-directory> -o <output.mppcdisc> [options]\n"
        "  mppcburner inspect <file.mppcdisc>\n"
        "  mppcburner bake-texture <disc-directory> <source> -o <output.mppctex> [--baker PATH]\n"
        "  mppcburner setup <tool>\n"
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
        "  setup         Install a pinned tool once, per user, under\n"
        "                ${XDG_DATA_HOME:-~/.local/share}/3dmppc. Tools: web (Emscripten).\n"
        "                A second run installs nothing.\n"
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
        "                           against. Default: pdk/include next to mppcburner.\n"
        "  -l, --pdklib PATH        Disc-side SDK include directory.\n"
        "                           Default: pdk/lib/include next to mppcburner.\n"
        "  -b, --baker PATH         directory of the bakers (mppctexbaker, mppcmusbaker, mppcmodbaker),\n"
        "                           or a file in it. Default: next to mppcburner, then $PATH.\n"
        "  -j, --jobs N             Parallel compile jobs. Default: cmake decides.\n"
        "  -k, --keep-build[=PATH]  Keep the generated CMake project instead of\n"
        "                           deleting it. PATH must be attached with '='.\n"
        "  -m, --map PATH           After a build that succeeded, write which entry\n"
        "                           of the disc each of its files became to PATH.\n"
        "  -t, --target NAME        What to write: disc (default): one .mppcdisc at -o, or\n"
        "                           an unpacked directory at -u; player: a directory at -o\n"
        "                           with the player console and the .mppcdisc. player takes\n"
        "                           no -u.\n"
        "\n"
        "Options (bake-texture):\n"
        "  -o, --output PATH        .mppctex file to write. Required.\n"
        "  -b, --baker PATH         directory of the texture baker (mppctexbaker),\n"
        "                           or a file in it. Default: next to mppcburner, then $PATH.\n"
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
// setup's own bit, for the same reason.
constexpr uint32_t RV_BURNER_MASK_SETUP = 1u << 3;

// Every option the tool has, declared once. Letters are unique tool-wide, which
// is what lets a single switch store the options of every subcommand.
constexpr rv_burner_option_spec OPTIONS[] = {
    { 'h',
        "help",
        no_argument,
        "print this text",
        RV_BURNER_MASK_BUILD | RV_BURNER_MASK_INSPECT | RV_BURNER_MASK_BAKE | RV_BURNER_MASK_SETUP },
    { 'o',
        "output",
        required_argument,
        "image to write (required unless --unpacked)",
        RV_BURNER_MASK_BUILD | RV_BURNER_MASK_BAKE },
    { 'u', "unpacked", required_argument, "write an unpacked disc directory instead", RV_BURNER_MASK_BUILD },
    { 'p', "pdk", required_argument, "PDK include directory the disc compiles against", RV_BURNER_MASK_BUILD },
    { 'l', "pdklib", required_argument, "disc-side SDK include directory", RV_BURNER_MASK_BUILD },
    { 'b', "baker", required_argument, "directory of the bakers, or a file in it", RV_BURNER_MASK_BUILD | RV_BURNER_MASK_BAKE },
    { 'j', "jobs", required_argument, "parallel compile jobs", RV_BURNER_MASK_BUILD },
    { 'k', "keep-build", optional_argument, "keep the generated CMake project, optionally at PATH", RV_BURNER_MASK_BUILD },
    { 'm', "map", required_argument, "write the source-to-entry map to PATH", RV_BURNER_MASK_BUILD },
    { 't', "target", required_argument, "what to write: disc (default) or player", RV_BURNER_MASK_BUILD },
};

static int nullable_handler(const rv_burner_options &)
{
    rv_pdktools::print_usage(stderr);
    return RV_OK;
}

// Every subcommand, plus the three spellings of help. A new command is a row
// here, never a branch in main.
constexpr rv_burner_command_spec COMMANDS[] = {
    { "build", RV_BURNER_MASK_BUILD, 1, rv_burner_build_run },
    { "inspect", RV_BURNER_MASK_INSPECT, 1, rv_burner_inspect_run },
    { "bake-texture", RV_BURNER_MASK_BAKE, k_bake_texture_operands, rv_burner_bake_texture_run },
    { "setup", RV_BURNER_MASK_SETUP, 1, rv_burner_setup_run },
    { "help", 0, 0, nullable_handler },
    { "-h", 0, 0, nullable_handler },
    { "--help", 0, 0, nullable_handler },
};
} // namespace rv_pdktools

int main(int argc, char **argv)
{
    if (argc < k_min_argc) {
        rv_pdktools::print_usage(stderr);
        return RV_ERR_INVAL;
    }

    const std::string_view command_name = argv[1];
    // One line a front end can show and compare (the editor's diagnostics).
    if (command_name == "--version") {
        rv_pdklib::rv_fprintf(stdout, "mppcburner %d.%d\n", RV_MPPC_VER_MAJOR, RV_MPPC_VER_MINOR);
        return RV_OK;
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
        return RV_ERR_INVAL;
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
            return RV_OK;
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
            if (*optarg == '\0') {
                rv_pdktools::rv_burner_print_error("--pdk needs a PATH");
                return RV_ERR_INVAL;
            }
            burner_options.pdk_dir = optarg;
            break;
        }
        case 'l': {
            if (*optarg == '\0') {
                rv_pdktools::rv_burner_print_error("--pdklib needs a PATH");
                return RV_ERR_INVAL;
            }
            burner_options.pdklib_dir = optarg;
            break;
        }
        case 'b': {
            burner_options.baker = optarg;
            break;
        }
        case 'j': {
            char *end = nullptr;
            const long value = std::strtol(optarg, &end, k_decimal_base);
            if (*optarg == '\0' || *end != '\0' || value <= 0 || value > INT_MAX) {
                rv_pdktools::rv_burner_print_error("invalid --jobs value '" + std::string(optarg) + "'");
                return RV_ERR_INVAL;
            }
            burner_options.jobs = static_cast<int>(value);
            break;
        }
        case 'm': {
            burner_options.map = optarg;
            break;
        }
        case 't': {
            if (*optarg == '\0') {
                rv_pdktools::rv_burner_print_error("--target needs a NAME");
                return RV_ERR_INVAL;
            }
            burner_options.target = optarg;
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
            return RV_ERR_INVAL;
        }
        }
    }

    // Exactly the declared number of operands: getopt left them from optind on.
    if (cmd->operands != local_argc - optind) {
        rv_pdktools::rv_burner_print_error(std::string(command_name) + " expects " + std::to_string(cmd->operands) +
            " operand(s), got " + std::to_string(local_argc - optind));
        rv_pdktools::print_usage(stderr);
        return RV_ERR_INVAL;
    }

    // Nothing to read for an operandless command: argv[optind] is its nullptr.
    // A second operand (bake-texture only) is the source to bake.
    if (cmd->operands >= 1) {
        burner_options.operand = local_argv[optind];
    }
    if (cmd->operands >= k_bake_texture_operands) {
        burner_options.bake_source = local_argv[optind + 1];
    }

    if (0 != (cmd->mask & rv_pdktools::RV_BURNER_MASK_BAKE) && burner_options.output.empty()) {
        rv_pdktools::rv_burner_print_error("bake-texture requires -o <output.mppctex>");
        return RV_ERR_INVAL;
    }

    // Only build compiles against the PDK; the other commands never read these paths.
    if (0 != (cmd->mask & rv_pdktools::RV_BURNER_MASK_BUILD)) {
        const int rc = rv_pdktools::resolve_pdk_dirs(burner_options);
        if (RV_OK != rc) {
            return rc;
        }
    }

    return cmd->handler(burner_options);
}
