// Probes this machine (RAM, kernel features, subsystem readiness) and reports
// what came up before any disc is touched.
#pragma once

#include <cstdint>

#include "rv_pboot_args.hpp"

namespace rv_3dmppc
{

class rv_pchost;

// Bytes the kernel estimates can be handed to a new application without
// swapping — /proc/meminfo MemAvailable, per Documentation/filesystems/proc.rst.
// This is an estimate, not a promise: a later allocation may still fail.
// Returns the byte count, or -1 when it cannot be determined.
int64_t rv_pboot_mode_available_ram();

// What was found out about this machine and this run: which subsystems
// actually came up, and how much memory the kernel says is available.
struct rv_pboot_mode_info {
    // rv_pboot_mode_available_ram(), or -1 when it could not be determined.
    int64_t ram_available = -1;
    // False when --headless, or when video refused to come up.
    bool video_enabled = true;
    // False when --no-audio, or when no device could be opened.
    bool audio_enabled = true;
};

// Runs the MADV_POPULATE_WRITE probe, host.prepare(...), the staging probe
// (which runs only when a disc path was given), and fills `out`.
// Returns RV_OK, or a negative rv_err when a hard requirement failed.
int64_t rv_pboot_mode_prepare(const rv_pboot_args &args, rv_pchost &host, rv_pboot_mode_info &out);

// Reports the preparation. States what the mode is ready to offer; it must
// not be read as any disc having been found compatible yet.
void rv_pboot_mode_report(const rv_pboot_args &args, const rv_pchost &host, const rv_pboot_mode_info &machine);

} // namespace rv_3dmppc
