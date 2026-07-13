// 3dmppc entry point: power on the console and insert the Solidmaid disc.
//
// The console (src/platform, src/core, src/gpu) is game-agnostic; all game logic
// lives behind the Disc ABI in src/game. See docs/README.md for the console↔disc
// split and docs/mppcdisc/solid/ for the game design this disc implements.
//
// Flags:
//   --scale N     window magnification over native 320×240 (default 3)
//   --headless    run without a window (smoke test); pair with --frames
//   --frames N    stop after N frames (0 = run until quit)
//   --fixed-step  use a fixed 1/60 dt (reproducible)
#include <cstdlib>
#include <cstring>

#include "game/solid.hpp"
#include "platform/console.hpp"

int main(int argc, char** argv) {
    rv_3dmppc::ConsoleConfig cfg;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--headless") == 0) {
            cfg.headless = true;
        } else if (std::strcmp(argv[i], "--fixed-step") == 0) {
            cfg.fixedStep = true;
        } else if (std::strcmp(argv[i], "--scale") == 0 && i + 1 < argc) {
            cfg.scale = std::atoi(argv[++i]);
        } else if (std::strcmp(argv[i], "--frames") == 0 && i + 1 < argc) {
            cfg.maxFrames = std::atoi(argv[++i]);
        }
    }

    rv_3dmppc::Console console(cfg);
    rv_3dmppc::SolidDisc disc;
    return console.run(disc);
}
