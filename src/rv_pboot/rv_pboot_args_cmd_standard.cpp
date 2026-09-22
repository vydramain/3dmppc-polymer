// The player build's half: there is no such option and no line about one. The
// row is the terminator itself, so getopt_long stops before it and answers
// --dev the way it answers any name it does not know.
#include "rv_pboot_args_cmd.hpp"

namespace rv_3dmppc
{

const struct option RV_PBOOT_ARGS_CMD_OPT = { nullptr, 0, 0, 0 };

const char *const RV_PBOOT_ARGS_CMD_USAGE = "";

const char *const RV_PBOOT_ARGS_CMD_PAUSE_HINT = "";

} // namespace rv_3dmppc
