// SIGINT/SIGTERM -> a flag the frame loop polls. Process state only, with no
// dependency on any platform library, installed before a platform gets a
// chance to claim these signals for itself (see the header for why order
// matters).
#include "rv_pconsole/platform/rv_pcsignals.hpp"

#include <csignal>

namespace rv_3dmppc
{

namespace
{

// volatile sig_atomic_t: the only type the standard guarantees is safe to
// touch from a signal handler and read back on the main thread without a
// data race.
volatile std::sig_atomic_t rv_pcsignals_quit_flag = 0;

void rv_pcsignals_handler(int /*signum*/)
{
    rv_pcsignals_quit_flag = 1;
}

} // namespace

void rv_pcsignals_install()
{
    struct sigaction action {};
    action.sa_handler = rv_pcsignals_handler;
    sigemptyset(&action.sa_mask);
    // SA_RESETHAND: one shot per signal. A second Ctrl+C while shutdown is
    // stuck falls through to the default disposition and kills the process.
    action.sa_flags = SA_RESETHAND;

    sigaction(SIGINT, &action, nullptr);
    sigaction(SIGTERM, &action, nullptr);
}

bool rv_pcsignals_quit_requested()
{
    return rv_pcsignals_quit_flag != 0;
}

} // namespace rv_3dmppc
