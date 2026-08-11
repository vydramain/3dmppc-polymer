// 3dmppc entry point
//
// The console is game-agnostic; all game logic lives behind on the Disc.
// See docs/README.md for the console/disc relations.
//
// The command line is documented in one place, rv_console_print_usage() in
// rv_infra/rv_console_print.hpp, so the help text cannot drift away from a
// comment nobody prints.
#include <getopt.h>

#include <cstdlib>
#include <format>

#include "rv_dmain/rv_dmain.hpp"
#include "rv_infra/rv_console_print.hpp"
#include "pdklib/rv_logs.hpp"
#include "rv_pconsole/rv_pconsole.hpp"
#include "rv_pconsole/rv_pconsole_conf.hpp"

int main(int argc, char** argv) {
    rv_3dmppc::rv_pconsole_conf conf;

    // Note what is NOT here: any game's name. The console mounts whatever
    // medium it is pointed at and boots the disc it is handed on the command
    // line; with nothing at all it runs the built-in skeleton against an empty
    // drive.
    static struct option long_opts[] = {{"headless", no_argument, 0, 'H'},
                                        {"fixed-step", no_argument, 0, 'F'},
                                        {"scale", required_argument, 0, 's'},
                                        {"frames", required_argument, 0, 'n'},
                                        {"disc", required_argument, 0, 'd'},
                                        {"memcard", required_argument, 0, 'm'},
                                        {"mute", no_argument, 0, 'M'},
                                        {"dump-frame", required_argument, 0, 'D'},
                                        {0, 0, 0, 0}};

    int c;
    while ((c = getopt_long(argc, argv, "HFMs:n:d:m:D:", long_opts, NULL)) != -1) {
        switch (c) {
            case 'H':
                conf.params.headless = true;
                break;
            case 'F':
                conf.params.fixed_step = true;
                break;
            case 'M':
                conf.ca.mute = true;
                break;
            case 's':
                conf.params.scale = atoi(optarg);
                break;
            case 'n':
                conf.params.max_frames = atoi(optarg);
                break;
            case 'd':
                conf.cd.medium_path = optarg;
                break;
            case 'm':
                conf.cm.image_path = optarg;
                break;
            case 'D':
                conf.params.dump_frame_path = optarg;
                break;
            case '?':
                // getopt has already named the offending option on stderr.
                rv_3dmppc::rv_console_print_usage(stderr);
                return 2;
        }
    }

    // getopt_long has left optind on the first thing that was not a flag. One
    // positional argument is expected — the disc — and more than one is a typo
    // worth refusing rather than silently ignoring.
    const char* disc_path = (optind < argc) ? argv[optind] : nullptr;
    if (optind + 1 < argc) {
        rv_3dmppc::rv_console_print_error(
            std::format("expected at most one disc path, got {}", argc - optind));
        rv_3dmppc::rv_console_print_usage(stderr);
        return 2;
    }

    rv_3dmppc::rv_pconsole console(conf);

    if (disc_path != nullptr) {
        // The console owns the loaded disc; it lives exactly as long as the
        // console does, and its teardown (disc_shutdown, destroy, dlclose,
        // unlink) happens when this scope ends. Nothing here holds the pointer
        // past that.
        rv_pdk::rv_de* disc = console.disc_load(disc_path);
        if (disc == nullptr) {
            // The loader has already said, precisely, what was wrong with it.
            // TODO(rv_log_escape): the console is the only caller of this, so it
            // does not belong in pdklib. Find it a console-side home. It must
            // stay a call-site step either way — once the message is formatted,
            // an injected newline is indistinguishable from one we wrote.
            rv_3dmppc::rv_console_print_error(
                std::format("refusing to boot '{}'", rv_pdklib::rv_log_escape(disc_path)));
            return 1;
        }
        return static_cast<int>(console.disc_run(*disc) < 0 ? 1 : 0);
    }

    // No disc in the machine: the built-in service test of the hardware. It is
    // linked into the console on purpose — it is how the console is checked
    // without a game, and it is not a game itself. `service` is a DISC's
    // namespace, not the machine's, and this is the only line in the console
    // that names a disc: the service test is embedded deliberately.
    rv_service::rv_dmain disc;
    return static_cast<int>(console.disc_run(disc) < 0 ? 1 : 0);
}
