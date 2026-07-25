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
//   --disc PATH   medium to mount in the drive (empty = no disc inserted)
//   --memcard P   memory-card image (default: memcard.mppccard here)
//   --mute        silence the audio output stage
//   --dump-frame P  write the last presented frame to P as a binary PPM
#include <getopt.h>

#include <cstdlib>

#include "rv_dmain/rv_dmain.hpp"
#include "rv_pconsole/rv_pconsole.hpp"
#include "rv_pconsole/rv_pconsole_conf.hpp"

int main(int argc, char** argv) {
    rv_3dmppc::rv_pconsole_conf conf;

    // NEUROSLOP-BEGIN (claude-opus-5)
    // Note what is NOT here: any game's name. The console mounts whatever
    // medium it is pointed at and boots the disc it was built with; with no
    // --disc it runs the built-in skeleton against an empty drive.
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
    // NEUROSLOP-END

    rv_3dmppc::rv_pconsole console(conf);
    rv_3dmppc::rv_dmain disc;  // Will be replaced with rv_pconsole.disc_load(path)
    return console.disc_run(disc);
}
