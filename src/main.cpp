// 3dmppc entry point
//
// The console is game-agnostic; all game logic lives behind on the Disc.
//
// The startup sequence is rv_pboot_run() in rv_pboot/rv_pboot.cpp. It calls
// the rv_pboot_<step> files in this order: args, modes, mode, budget, check,
// conf. The command line is documented in one place,
// rv_console_print_usage() in rv_pboot/rv_pboot_args.hpp, so the help text
// cannot drift away from a comment nobody prints.
#include "rv_pboot/rv_pboot.hpp"

int main(int argc, char **argv)
{
    return rv_3dmppc::rv_pboot_run(argc, argv);
}
