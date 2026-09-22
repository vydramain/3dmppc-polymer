// The option that attaches the command channel
// (rv_pconsole/platform/rv_pccmdchan.hpp), and the help paragraph describing
// it. Both arrive from the file CMake selects, so the shared getopt table and
// the shared help text carry no build condition of their own.
#pragma once

#include <getopt.h>

namespace rv_3dmppc
{

// The last row before the table's terminator, on purpose: a player build fills
// it WITH the terminator, so getopt_long stops there and never learns the name.
extern const struct option RV_PBOOT_ARGS_CMD_OPT;

// Printed where --dev belongs among the options; empty in a player build.
extern const char *const RV_PBOOT_ARGS_CMD_USAGE;

// Tail of the refusal a run earns by asking for --paused with no way to lift
// it (rv_pboot.cpp). Empty in a player build, which cannot name --dev.
extern const char *const RV_PBOOT_ARGS_CMD_PAUSE_HINT;

} // namespace rv_3dmppc
