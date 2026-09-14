// Normal-shutdown requests from the operating system. Process-wide rather than
// a platform service: SIGINT and SIGTERM mean the same thing on every platform,
// so one handler serves them all. The handler only sets a flag; the frame loop
// sees it through rv_pcplatform::quit_requested() and leaves by its ordinary
// path, so disc_shutdown, the loader teardown and the staging cleanup all run.
//
// Installed by boot BEFORE any platform comes up. That order also keeps a
// platform library off these signals: SDL only claims a signal that is still
// at its default disposition.
#pragma once

namespace rv_3dmppc
{

// Route SIGINT and SIGTERM to the flag below. One-shot per signal
// (SA_RESETHAND): a second Ctrl+C while a shutdown is stuck kills the process
// the default way.
void rv_pcsignals_install();

// True once SIGINT or SIGTERM arrived after rv_pcsignals_install().
bool rv_pcsignals_quit_requested();

} // namespace rv_3dmppc
