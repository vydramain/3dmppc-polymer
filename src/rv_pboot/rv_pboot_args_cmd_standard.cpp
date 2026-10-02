// The player build's half: there are no such options and no line about them. The
// rows are terminators themselves, so getopt_long stops before them and answers
// --dev or --frame-fd the way it answers any name it does not know.
#include "rv_pboot_args_cmd.hpp"

namespace rv_3dmppc
{

const struct option RV_PBOOT_ARGS_CMD_OPTS[2] = { { nullptr, 0, 0, 0 }, { nullptr, 0, 0, 0 } };

const char *const RV_PBOOT_ARGS_CMD_USAGE = "";

const char *const RV_PBOOT_ARGS_CMD_PAUSE_HINT = "";

} // namespace rv_3dmppc
