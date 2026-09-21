#include "rv_pboot_args.hpp"

#include <getopt.h>

#include <charconv>
#include <cstddef>
#include <format>
#include <iterator>
#include <string>
#include <string_view>

#include "pdklib/rv_logs/rv_logs.hpp"
#include "rv_pboot_modes.hpp"
#include "rv_pconsole/rv_pcslots.hpp"

namespace rv_3dmppc
{

namespace
{

// Strict non-negative decimal. Anything else, empty, a sign, letters, trailing
// junk, a value too large for the type, is a bad argument, not a zero.
bool parse_u64(const char* text, uint64_t& out) {
    if (text == nullptr || text[0] == '\0' || text[0] == '+' || text[0] == '-') return false;
    const char* end = text + std::string_view(text).size();
    auto [ptr, ec] = std::from_chars(text, end, out);
    return ec == std::errc() && ptr == end;
}

// Joins a table's `.name` column as "a or b" (two rows) or "a, b or c"
// (three or more): the same shape --mode_<slot> error messages use, just
// without the ", available: " prefix.
template <typename Table>
std::string join_names_or(const Table &table) {
    std::string result;
    const std::size_t n = std::size(table);
    std::size_t i = 0;
    for (const auto &row : table) {
        if (i > 0) {
            result += (i + 1 == n) ? " or " : ", ";
        }
        result += row.name;
        ++i;
    }
    return result;
}

// Every bad argument ends the same way: name it, print the usage, exit 2.
// Written once because it was written five times, and the fifth copy is where
// one of them stops matching the others.
bool refuse(const std::string &what, int &exit_code) {
    rv_3dmppc::rv_console_print_error(what);
    rv_3dmppc::rv_console_print_usage(stderr);
    exit_code = 2;
    return false;
}

// A numeric option, refused by its own name. `floor` is the smallest value the
// option accepts, so --scale can reject 0 without a second check at the call.
bool option_u64(const char *name, const char *text, uint64_t floor, uint64_t &out, int &exit_code) {
    if (!parse_u64(text, out) || out < floor) {
        return refuse(std::format("bad value for --{}: '{}'", name, rv_pdklib::rv_log_escape(text)),
            exit_code);
    }
    return true;
}

}  // namespace

void rv_console_print_usage(std::FILE *stream)
{
    // The sentence wraps after the first preset, same as the literal help
    // text used to: rewrap " and " onto its own indented line.
    std::string presets = rv_pboot_builtin_presets_summary();
    const std::size_t and_pos = presets.rfind(" and ");
    if (and_pos != std::string::npos) {
        presets.replace(and_pos, 5, "\n                       and ");
    }
    const std::string platform_list = join_names_or(RV_PCSLOTS_PLATFORM);
    const std::string ca_list = join_names_or(RV_PCSLOTS_CA);
    const std::string cv_list = join_names_or(RV_PCSLOTS_CV);
    const std::string cio_list = join_names_or(RV_PCSLOTS_CIO);
    const std::string cl_list = join_names_or(RV_PCSLOTS_CL);
    const std::string cd_list = join_names_or(RV_PCSLOTS_CD);
    const std::string cm_list = join_names_or(RV_PCSLOTS_CM);

    rv_pdklib::rv_fprintf(stream,
        "3dmppc - the console: mount a disc and run it\n"
        "\n"
        "Usage:\n"
        "  3dmppc [options] [<disc.mppcdisc>]\n"
        "\n"
        "The positional argument is the disc to boot: the console mounts that\n"
        "archive, loads the code inside it and runs it. With no disc it falls\n"
        "back to the built-in rv_dmain, a service test of the hardware rather\n"
        "than a game.\n"
        "\n"
        "Options:\n"
        "  -s, --scale N        Window magnification over native 320x240.\n"
        "                       Default: 3.\n"
        "  -n, --frames N       Stop after N frames. 0 runs until quit.\n"
        "  -F, --fixed-step     Fast run: no real-time wait and no audio\n"
        "                       output. Every mode steps the machine by\n"
        "                       1/60 s per frame, so runs stay reproducible.\n"
        "  -d, --disc PATH      Assets DIRECTORY for the BUILT-IN disc, which\n"
        "                       carries no medium of its own. A disc given\n"
        "                       positionally brings its own medium, archive or\n"
        "                       directory, so the two may not be combined.\n"
        "  -m, --memcard PATH   Memory-card image. Default: memcard.mppccard\n"
        "                       next to the 3dmppc binary.\n"
        "  -M, --mute           Silence the audio output stage.\n"
        "  -D, --dump-frame P   Write the last rendered frame to P as a binary\n"
        "                       PPM. Refused when cv is null.\n"
#if RV_DEVTOOLS
        // A player build does not even reach rv_console_print_usage() with
        // this line in it: --dev is not in its getopt table at all (see
        // rv_pboot_args), so printing it there would advertise an option the
        // build cannot parse, let alone act on.
        "      --dev            Attach the development channel to stdin and\n"
        "                       stdout: pause, step, reload of the lua entry\n"
        "                       and state inspection. WHAT this console can do\n"
        "                       is decided by its build (-D3DMPPC_DEVTOOLS);\n"
        "                       this only says where to speak. A console built\n"
        "                       without the development runtime refuses it.\n"
#endif
        "      --paused         Start with the frame loop stopped, before frame\n"
#if RV_DEVTOOLS
        "                       0. Lift it with the Pause key, or with the\n"
        "                       resume/step requests when --dev is given.\n"
#else
        "                       0. Lift it with the Pause key.\n"
#endif
        "      --mode=NAME      Preset: the platform plus one implementation\n"
        "                       per slot. Built in: %s. Default:\n"
        "                       default.\n"
        "      --mode_platform=IMPL\n"
        "                       Override the platform of the preset. IMPL is\n"
        "                       %s. null runs the same machine with\n"
        "                       no window, no gamepads and no audio device.\n"
        "      --mode_ca=IMPL   Override the ca slot of the preset. IMPL is\n"
        "                       %s.\n"
        "      --mode_cv=IMPL   Override the cv slot of the preset. IMPL is\n"
        "                       %s. A run without a window ends by\n"
        "                       --frames, by the disc, or by Ctrl+C.\n"
        "      --mode_cio=IMPL  Override the cio slot of the preset. IMPL is\n"
        "                       %s.\n"
        "      --mode_cl=IMPL   Override the cl slot of the preset. IMPL is\n"
        "                       %s.\n"
        "      --mode_cd=IMPL   Override the cd slot of the preset. IMPL is\n"
        "                       %s.\n"
        "      --mode_cm=IMPL   Override the cm slot of the preset. IMPL is\n"
        "                       %s.\n",
        presets.c_str(), platform_list.c_str(), ca_list.c_str(), cv_list.c_str(), cio_list.c_str(),
        cl_list.c_str(), cd_list.c_str(), cm_list.c_str());
}

namespace {

// Which medium this run mounts, decided after getopt has taken every flag it
// recognises. Its own function because it is its own question - the loop above
// only collects strings - and because the two ways of getting it wrong each
// need a sentence.
bool rv_pboot_args_disc(int argc, char** argv, rv_pboot_args& args, int& exit_code) {
    // getopt_long has left optind on the first thing that was not a flag. One
    // positional argument is expected - the disc - and more than one is a typo
    // worth refusing rather than silently ignoring.
    args.disc_path = (optind < argc) ? argv[optind] : nullptr;
    // A positional disc inserts its OWN medium (rv_pboot_run), which would
    // overwrite whatever --disc mounted - so the two together are a run whose
    // -d did nothing at all. Refusing says so; the old silent override left
    // the developer reading a `medium=fixed` status they had asked to be live.
    if (args.disc_path != nullptr && !args.medium_path.empty()) {
        return refuse("--disc names the assets directory for the BUILT-IN disc; a disc given "
                      "positionally brings its own medium. Give one or the other.",
            exit_code);
    }
    if (optind + 1 < argc) {
        return refuse(std::format("expected at most one disc path, got {}", argc - optind), exit_code);
    }
    return true;
}

}  // namespace

bool rv_pboot_args_parse(int argc, char** argv, rv_pboot_args& args, int& exit_code) {
    // There is no game's name here. The console mounts whatever medium it is
    // pointed at and boots the disc it is handed on the command line; with
    // nothing at all it runs the built-in skeleton against an empty drive.
    static struct option long_opts[] = {{"fixed-step", no_argument, 0, 'F'},
                                        {"scale", required_argument, 0, 's'},
                                        {"frames", required_argument, 0, 'n'},
                                        {"disc", required_argument, 0, 'd'},
                                        {"memcard", required_argument, 0, 'm'},
                                        {"mute", no_argument, 0, 'M'},
                                        {"dump-frame", required_argument, 0, 'D'},
                                        {"mode", required_argument, 0, 'o'},
                                        {"mode_platform", required_argument, 0, 'p'},
                                        {"mode_ca", required_argument, 0, 'a'},
                                        {"mode_cv", required_argument, 0, 'v'},
                                        {"mode_cio", required_argument, 0, 'i'},
                                        {"mode_cl", required_argument, 0, 'l'},
                                        {"mode_cd", required_argument, 0, 'c'},
                                        {"mode_cm", required_argument, 0, 'k'},
#if RV_DEVTOOLS
                                        {"dev", no_argument, 0, 'E'},
#endif
                                        {"paused", no_argument, 0, 'Y'},
                                        {0, 0, 0, 0}};

    int c;
    while ((c = getopt_long(argc, argv, "FMs:n:d:m:D:", long_opts, NULL)) != -1) {
        switch (c) {
            case 'F':
                args.fixed_step = true;
                break;
            case 'M':
                args.mute = true;
                break;
#if RV_DEVTOOLS
            case 'E':
                args.dev = true;
                break;
#endif
            case 'Y':
                args.loop_paused = true;
                break;
            case 'p':
                args.mode_platform = optarg;
                break;
            case 'a':
                args.mode_ca = optarg;
                break;
            case 'v':
                args.mode_cv = optarg;
                break;
            case 'i':
                args.mode_cio = optarg;
                break;
            case 'l':
                args.mode_cl = optarg;
                break;
            case 'c':
                args.mode_cd = optarg;
                break;
            case 'k':
                args.mode_cm = optarg;
                break;
            case 's':
                if (!option_u64("scale", optarg, 1, args.scale, exit_code)) return false;
                break;
            case 'n':
                if (!option_u64("frames", optarg, 0, args.max_frames, exit_code)) return false;
                break;
            case 'd':
                args.medium_path = optarg;
                break;
            case 'm':
                args.memcard_path = optarg;
                break;
            case 'D':
                args.dump_frame_path = optarg;
                break;
            case 'o':
                args.mode = optarg;
                break;
            case '?':
                // getopt has already named the offending option on stderr.
                rv_3dmppc::rv_console_print_usage(stderr);
                exit_code = 2;
                return false;
            default:
                break;
        }
    }

    // Whether --paused can be lifted at all depends on the platform and the cv
    // slot, which are not resolved yet - so that refusal lives in rv_pboot_run,
    // not here. This file only collects what getopt produced.

    // Which slot's implementation actually resolves --mode/--mode_<slot> to,
    // and whether cv ends up null (so --dump-frame must be refused), is not
    // known until rv_pboot_modes_resolve runs - this file only collects the
    // raw strings.

    return rv_pboot_args_disc(argc, argv, args, exit_code);
}

}  // namespace rv_3dmppc
