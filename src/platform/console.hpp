// The 3dmppc console: boots the hardware services (window/presenter, input,
// audio, memory card) and runs a disc's frame loop against them.
//
// INTERFACE owned by the orchestrator; implementation (console.cpp) is Agent A.
#pragma once

namespace rv_3dmppc {

class rv_Disc;

struct rv_ConsoleConfig {
    int scale = 3;           // window magnification over native 320×240
    bool headless = false;   // no window/present — for automated smoke tests
    int maxFrames = 0;       // if > 0, stop after this many frames (0 = run until quit)
    bool fixedStep = false;  // headless: use a fixed 1/60 dt for reproducibility
};

class rv_Console {
   public:
    explicit rv_Console(const rv_ConsoleConfig& cfg) : cfg_(cfg) {}

    // Boot services, run the disc to completion, tear down. Returns process code.
    int run(rv_Disc& disc);

   private:
    rv_ConsoleConfig cfg_;
};

}  // namespace rv_3dmppc
