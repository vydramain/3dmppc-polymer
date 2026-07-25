// 3dmppc entry point
//
// The console is game-agnostic; all game logic lives behind on the Disc.
// See docs/README.md for the console/disc relations.
//
// Usage:
//   3dmppc [flags] [DISC.mppcdisc]
//
// The first positional argument is the disc to boot: the console mounts that
// archive, loads the code inside it and runs it. With no disc it falls back to
// the built-in rv_dmain — a service test of the hardware, not a game.
//
// Flags:
//   --scale N     window magnification over native 320×240 (default 3)
//   --headless    run without a window (smoke test); pair with --frames
//   --frames N    stop after N frames (0 = run until quit)
//   --fixed-step  use a fixed 1/60 dt (reproducible)
//   --disc PATH   medium to mount in the drive (empty = no disc inserted).
//                 A DIRECTORY of loose assets — the development shortcut, kept
//                 because it needs no packaging step. A packaged .mppcdisc goes
//                 in the positional argument instead, and brings its own medium.
//   --memcard P   memory-card image (default: memcard.mppccard here)
//   --mute        silence the audio output stage
//   --dump-frame P  write the last presented frame to P as a binary PPM
#include <getopt.h>

#include <cstdlib>
#include <format>
#include <string>

#include "rv_dmain/rv_dmain.hpp"
#include "rv_infra/rv_log.hpp"
#include "rv_pconsole/rv_pconsole.hpp"
#include "rv_pconsole/rv_pconsole_conf.hpp"

// NEUROSLOP-BEGIN (claude-opus-5)
namespace rv_3dmppc {
namespace {

// The log macros name rv_log_emit unqualified, so they only compile from inside
// the namespace — and main() is, by definition, outside it.
void rv_main_log_error(const std::string& message) { RV_LOG_ERR("main", "{}", message); }

}  // namespace
}  // namespace rv_3dmppc
// NEUROSLOP-END

int main(int argc, char** argv) {
    rv_3dmppc::rv_pconsole_conf conf;

    // NEUROSLOP-BEGIN (claude-opus-5)
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
                return 2;
        }
    }

    // getopt_long has left optind on the first thing that was not a flag. One
    // positional argument is expected — the disc — and more than one is a typo
    // worth refusing rather than silently ignoring.
    const char* disc_path = (optind < argc) ? argv[optind] : nullptr;
    if (optind + 1 < argc) {
        rv_3dmppc::rv_main_log_error(
            std::format("expected at most one disc path, got {}", argc - optind));
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
            rv_3dmppc::rv_main_log_error(
                std::format("refusing to boot '{}'", rv_3dmppc::rv_log_escape(disc_path)));
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
    // NEUROSLOP-END
}
