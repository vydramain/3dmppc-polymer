// ─── NEUROSLOP ────────────────────────────────────────────────────────────────
// Сгенерировано Claude (claude-opus-5). Не проверено человеком.
// Ревизия: stage 10 — командная строка mppcburner: подкоманды build и inspect.
// ──────────────────────────────────────────────────────────────────────────────
//
// mppcburner — the developer's side of the disc medium. A prepared directory
// goes in (sources, a disc.toml, artwork) and one .mppcdisc comes out, which is
// the only thing the console ever knows how to load.
//
// Two subcommands, because they are two different jobs with two different
// contracts: `build` writes a file and talks to a human, `inspect` writes
// nothing and talks to a script.
//
// STDOUT IS DATA, STDERR IS TALK. `inspect` puts the manifest and the entry
// listing on stdout and nothing else, so `mppcburner inspect x.mppcdisc | grep
// tex` is a sentence with one meaning. `build` puts its whole [n/4] ladder on
// stderr, including the successful lines: its product is a FILE, not a stream,
// and a build that printed progress into a pipe would corrupt whatever the pipe
// was for. Every refusal, anywhere, exits non-zero.

#include <cstdio>
#include <cstdlib>
#include <string>
#include <string_view>
#include <vector>

#include "rv_build.hpp"

// Where a generated disc project looks for the contract and the disc-side SDK.
// Normally baked in by pdk/tools/mppcburner/CMakeLists.txt; the fallbacks keep this
// file compilable on its own, and are wrong often enough that --pdk / --pdklib
// exist.
#ifndef RV_BURNER_DEFAULT_PDK
#define RV_BURNER_DEFAULT_PDK "pdk/include"
#endif
#ifndef RV_BURNER_DEFAULT_PDKLIB
#define RV_BURNER_DEFAULT_PDKLIB "pdklib/include"
#endif

namespace rv_pdktools {
namespace {

void usage(std::FILE* stream) {
    std::fprintf(stream,
                 "usage: mppcburner build <disc-directory> -o <output.mppcdisc>\n"
                 "                        [--pdk PATH] [--pdklib PATH] [--baker PATH]\n"
                 "                        [--jobs N] [--keep-build[=PATH]]\n"
                 "       mppcburner inspect <file.mppcdisc>\n"
                 "\n"
                 "build   compile a disc directory and burn it into one .mppcdisc.\n"
                 "        Progress and diagnostics go to stderr; nothing to stdout.\n"
                 "\n"
                 "  -o, --output PATH   the .mppcdisc to write (required)\n"
                 "  --pdk PATH          include directory of the PDK the disc compiles\n"
                 "                      against (default: " RV_BURNER_DEFAULT_PDK
                 ")\n"
                 "  --pdklib PATH          include directory of the disc-side SDK\n"
                 "                      (default: " RV_BURNER_DEFAULT_PDKLIB
                 ")\n"
                 "  --baker PATH        mppcbaker to bake textures with (default: next to\n"
                 "                      mppcburner, then $PATH)\n"
                 "  --jobs N            parallel compile jobs (default: cmake decides)\n"
                 "  --keep-build[=PATH] keep the generated CMake project instead of\n"
                 "                      deleting it, optionally somewhere of your choosing\n"
                 "\n"
                 "inspect print the manifest, the entries and their sizes to stdout.\n"
                 "        Nothing is unpacked and nothing is written to disk.\n");
}

void say_error(const std::string& message) {
    std::fprintf(stderr, "mppcburner: error: %s\n", message.c_str());
}

// Take the value of an option that needs one, or refuse. `index` points at the
// option itself and is advanced onto the value.
bool take_value(int argc, char** argv, int& index, const char* option, std::string& out) {
    if (index + 1 >= argc) {
        say_error(std::string(option) + " needs a value");
        return false;
    }
    ++index;
    out = argv[index];
    return true;
}

int run_build(int argc, char** argv) {
    rv_build_options options;
    options.pdk_dir = RV_BURNER_DEFAULT_PDK;
    options.pdklib_dir = RV_BURNER_DEFAULT_PDKLIB;

    std::vector<std::string> positionals;
    for (int i = 0; i < argc; ++i) {
        const std::string_view argument = argv[i];
        if (argument == "-h" || argument == "--help") {
            usage(stdout);
            return 0;
        } else if (argument == "-o" || argument == "--output") {
            if (!take_value(argc, argv, i, "-o", options.output)) return 1;
        } else if (argument == "--pdk") {
            if (!take_value(argc, argv, i, "--pdk", options.pdk_dir)) return 1;
        } else if (argument == "--pdklib") {
            if (!take_value(argc, argv, i, "--pdklib", options.pdklib_dir)) return 1;
        } else if (argument == "--baker") {
            if (!take_value(argc, argv, i, "--baker", options.baker)) return 1;
        } else if (argument == "--jobs") {
            std::string value;
            if (!take_value(argc, argv, i, "--jobs", value)) return 1;
            options.jobs = std::atoi(value.c_str());
            if (options.jobs <= 0) {
                say_error("--jobs wants a positive number, got '" + value + "'");
                return 1;
            }
        } else if (argument == "--keep-build") {
            options.keep_build = true;
        } else if (argument.starts_with("--keep-build=")) {
            options.keep_build = true;
            options.build_dir =
                std::string(argument.substr(std::string_view("--keep-build=").size()));
            if (options.build_dir.empty()) {
                say_error("--keep-build= needs a directory after the '='");
                return 1;
            }
        } else if (!argument.empty() && argument.front() == '-') {
            say_error("unknown option '" + std::string(argument) + "'");
            usage(stderr);
            return 1;
        } else {
            positionals.push_back(std::string(argument));
        }
    }

    if (positionals.size() != 1) {
        say_error(positionals.empty() ? "build needs a disc directory"
                                      : "build takes exactly one disc directory");
        usage(stderr);
        return 1;
    }
    if (options.output.empty()) {
        say_error("build needs an output file (-o <output.mppcdisc>)");
        usage(stderr);
        return 1;
    }
    options.disc_dir = positionals.front();
    return rv_build_run(options);
}

int run_inspect(int argc, char** argv) {
    std::vector<std::string> positionals;
    for (int i = 0; i < argc; ++i) {
        const std::string_view argument = argv[i];
        if (argument == "-h" || argument == "--help") {
            usage(stdout);
            return 0;
        } else if (!argument.empty() && argument.front() == '-') {
            say_error("unknown option '" + std::string(argument) + "'");
            usage(stderr);
            return 1;
        } else {
            positionals.push_back(std::string(argument));
        }
    }
    if (positionals.size() != 1) {
        say_error(positionals.empty() ? "inspect needs a .mppcdisc file"
                                      : "inspect takes exactly one .mppcdisc file");
        usage(stderr);
        return 1;
    }
    return rv_inspect_run(positionals.front());
}

}  // namespace
}  // namespace rv_pdktools

int main(int argc, char** argv) {
    if (argc < 2) {
        rv_pdktools::usage(stderr);
        return 1;
    }
    const std::string_view command = argv[1];
    if (command == "-h" || command == "--help" || command == "help") {
        rv_pdktools::usage(stdout);
        return 0;
    }
    if (command == "build") {
        return rv_pdktools::run_build(argc - 2, argv + 2);
    }
    if (command == "inspect") {
        return rv_pdktools::run_inspect(argc - 2, argv + 2);
    }
    rv_pdktools::say_error("unknown subcommand '" + std::string(command) + "'");
    rv_pdktools::usage(stderr);
    return 1;
}
