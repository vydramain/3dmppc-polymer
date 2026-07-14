// 3dmppc entry point
//
// The console is game-agnostic; all game logic lives behind on the Disc.
// See docs/README.md for the console/disc relations.
//
// Flags:
//   --scale N     window magnification over native 320×240 (default 3)
//   --headless    run without a window (smoke test); pair with --frames
//   --frames N    stop after N frames (0 = run until quit)
//   --fixed-step  use a fixed 1/60 dt (reproducible)
#include <getopt.h>

#include <cstdlib>

#include "platform/console.hpp"
#include "solidmaid.hpp"

int main(int argc, char** argv) {
    rv_3dmppc::rv_ConsoleConfig cfg;

    static struct option long_opts[] = {{"headless", no_argument, 0, 'H'},
                                        {"fixed-step", no_argument, 0, 'F'},
                                        {"scale", required_argument, 0, 's'},
                                        {"frames", required_argument, 0, 'n'},
                                        {0, 0, 0, 0}};

    int c;
    while ((c = getopt_long(argc, argv, "HFs:n:", long_opts, NULL)) != -1) {
        switch (c) {
            case 'H':
                cfg.headless = true;
                break;
            case 'F':
                cfg.fixedStep = true;
                break;
            case 's':
                cfg.scale = atoi(optarg);
                break;
            case 'n':
                cfg.maxFrames = atoi(optarg);
                break;
            case '?':
                return 2;
        }
    }

    rv_3dmppc::rv_Console console(cfg);
    rv_3dmppc::SolidMaidDisc disc;
    return console.run(disc);
}
