// The development build's half: --dev exists and the help says so.
#include "rv_pboot_args_cmd.hpp"

namespace rv_3dmppc
{

const struct option RV_PBOOT_ARGS_CMD_OPT = { "dev", no_argument, 0, 'E' };

const char *const RV_PBOOT_ARGS_CMD_USAGE =
    "      --dev            Attach the development channel to stdin and\n"
    "                       stdout: pause, step, resume, reload of the lua\n"
    "                       entry and state inspection. The channel only says\n"
    "                       WHERE to speak; what this console can do was\n"
    "                       decided when it was built.\n";

const char *const RV_PBOOT_ARGS_CMD_PAUSE_HINT = ", and --dev was not given";

} // namespace rv_3dmppc
