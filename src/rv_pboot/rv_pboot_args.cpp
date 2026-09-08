#include "rv_pboot_args.hpp"

#include <getopt.h>

#include <charconv>
#include <format>
#include <string_view>

#include "pdklib/rv_logs/rv_logs.hpp"

namespace rv_3dmppc
{

namespace
{

// The only backend this console knows how to boot. A name outside this list
// is a bad argument (exit 2, usage printed), not a machine failure, the same
// kind of mistake as an unrecognized flag.
constexpr const char* kKnownModes[] = {"sdl3"};

bool is_known_mode(const std::string& mode) {
    for (const char* known : kKnownModes) {
        if (mode == known) return true;
    }
    return false;
}

// Strict non-negative decimal. Anything else, empty, a sign, letters, trailing
// junk, a value too large for the type, is a bad argument, not a zero.
bool parse_u64(const char* text, uint64_t& out) {
    if (text == nullptr || text[0] == '\0' || text[0] == '+' || text[0] == '-') return false;
    const char* end = text + std::string_view(text).size();
    auto [ptr, ec] = std::from_chars(text, end, out);
    return ec == std::errc() && ptr == end;
}

}  // namespace

bool rv_pboot_args_parse(int argc, char** argv, rv_pboot_args& args, int& exit_code) {
    // There is no game's name here. The console mounts whatever medium it is
    // pointed at and boots the disc it is handed on the command line; with
    // nothing at all it runs the built-in skeleton against an empty drive.
    static struct option long_opts[] = {{"headless", no_argument, 0, 'H'},
                                        {"fixed-step", no_argument, 0, 'F'},
                                        {"scale", required_argument, 0, 's'},
                                        {"frames", required_argument, 0, 'n'},
                                        {"disc", required_argument, 0, 'd'},
                                        {"memcard", required_argument, 0, 'm'},
                                        {"mute", no_argument, 0, 'M'},
                                        {"dump-frame", required_argument, 0, 'D'},
                                        {"mode", required_argument, 0, 'o'},
                                        {"no-audio", no_argument, 0, 'A'},
                                        {"selfcheck", no_argument, 0, 'Y'},
                                        {0, 0, 0, 0}};

    int c;
    while ((c = getopt_long(argc, argv, "HFMs:n:d:m:D:", long_opts, NULL)) != -1) {
        switch (c) {
            case 'H':
                args.headless = true;
                break;
            case 'F':
                args.fixed_step = true;
                break;
            case 'M':
                args.mute = true;
                break;
            case 'A':
                args.no_audio = true;
                break;
            case 's':
                if (!parse_u64(optarg, args.scale) || args.scale == 0) {
                    rv_3dmppc::rv_console_print_error(
                        std::format("bad value for --scale: '{}'", rv_pdklib::rv_log_escape(optarg)));
                    rv_3dmppc::rv_console_print_usage(stderr);
                    exit_code = 2;
                    return false;
                }
                break;
            case 'n':
                if (!parse_u64(optarg, args.max_frames)) {
                    rv_3dmppc::rv_console_print_error(
                        std::format("bad value for --frames: '{}'", rv_pdklib::rv_log_escape(optarg)));
                    rv_3dmppc::rv_console_print_usage(stderr);
                    exit_code = 2;
                    return false;
                }
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
            case 'Y':
                args.selfcheck = true;
                break;
            case '?':
                // getopt has already named the offending option on stderr.
                rv_3dmppc::rv_console_print_usage(stderr);
                exit_code = 2;
                return false;
        }
    }

    // --headless means no window and no rasterization (rv_pconsole::disc_run
    // skips frame_render() entirely), so a frame dump would only ever be an
    // empty frame. Refuse the combination here rather than write a useless
    // file.
    if (args.headless && !args.dump_frame_path.empty()) {
        rv_3dmppc::rv_console_print_error("--headless and --dump-frame cannot be combined");
        rv_3dmppc::rv_console_print_usage(stderr);
        exit_code = 2;
        return false;
    }

    // --selfcheck is only recorded here; it is acted on by the caller, so this
    // file gains no dependency on rv_pmem.
    if (args.selfcheck) {
        return true;
    }

    if (!is_known_mode(args.mode)) {
        rv_3dmppc::rv_console_print_error(
            std::format("unknown --mode '{}', available: sdl3", rv_pdklib::rv_log_escape(args.mode.c_str())));
        rv_3dmppc::rv_console_print_usage(stderr);
        exit_code = 2;
        return false;
    }

    // getopt_long has left optind on the first thing that was not a flag. One
    // positional argument is expected — the disc — and more than one is a typo
    // worth refusing rather than silently ignoring.
    args.disc_path = (optind < argc) ? argv[optind] : nullptr;
    if (optind + 1 < argc) {
        rv_3dmppc::rv_console_print_error(
            std::format("expected at most one disc path, got {}", argc - optind));
        rv_3dmppc::rv_console_print_usage(stderr);
        exit_code = 2;
        return false;
    }

    return true;
}

}  // namespace rv_3dmppc
