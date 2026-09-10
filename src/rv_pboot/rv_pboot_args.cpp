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
    static struct option long_opts[] = {{"fixed-step", no_argument, 0, 'F'},
                                        {"scale", required_argument, 0, 's'},
                                        {"frames", required_argument, 0, 'n'},
                                        {"disc", required_argument, 0, 'd'},
                                        {"memcard", required_argument, 0, 'm'},
                                        {"mute", no_argument, 0, 'M'},
                                        {"dump-frame", required_argument, 0, 'D'},
                                        {"mode", required_argument, 0, 'o'},
                                        {"mode_ca", required_argument, 0, 'a'},
                                        {"mode_cv", required_argument, 0, 'v'},
                                        {"mode_cio", required_argument, 0, 'i'},
                                        {"mode_cl", required_argument, 0, 'l'},
                                        {"selfcheck", no_argument, 0, 'Y'},
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

    // --selfcheck is only recorded here; it is acted on by the caller, so this
    // file gains no dependency on rv_pmem.
    if (args.selfcheck) {
        return true;
    }

    // Which slot's implementation actually resolves --mode/--mode_<slot> to,
    // and whether cv ends up null (so --dump-frame must be refused), is not
    // known until rv_pboot_modes_resolve runs — this file only collects the
    // raw strings.

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
