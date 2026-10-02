// The development build's half: --dev and --frame-fd exist and the help says so.
#include "rv_pboot_args_cmd.hpp"

namespace rv_3dmppc
{

const struct option RV_PBOOT_ARGS_CMD_OPTS[2] = {
    { "dev", no_argument, 0, 'E' },
    { "frame-fd", required_argument, 0, 'G' },
};

const char *const RV_PBOOT_ARGS_CMD_USAGE =
    "      --dev            Attach the development channel to stdin and\n"
    "                       stdout: pause, step, resume, reload of the lua\n"
    "                       entry and state inspection. The channel only says\n"
    "                       WHERE to speak; what this console can do was\n"
    "                       decided when it was built.\n"
    "      --frame-fd N     With --dev: open no window; write finished frames\n"
    "                       into the shared memory object on descriptor N and\n"
    "                       take pad buttons from the channel's pad request.\n";

const char *const RV_PBOOT_ARGS_CMD_PAUSE_HINT = ", and --dev was not given";

} // namespace rv_3dmppc
