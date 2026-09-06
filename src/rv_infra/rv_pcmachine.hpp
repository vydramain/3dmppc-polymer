#pragma once

#include <cstdint>

namespace rv_3dmppc
{

// Bytes the kernel estimates can be handed to a new application without
// swapping — /proc/meminfo MemAvailable, per Documentation/filesystems/proc.rst.
// This is an ESTIMATE, not a promise: a later allocation may still fail.
// Returns the byte count, or -1 when it cannot be determined.
int64_t rv_pcmachine_available_ram();

// What stage C found out about this machine and this run: which subsystems
// actually came up, and how much memory the kernel says is available.
struct rv_pcmachine_info {
    // rv_pcmachine_available_ram(), or -1 when it could not be determined.
    int64_t ram_available = -1;
    // False when --headless, or when video refused to come up.
    bool video_enabled = true;
    // False when --no-audio, or when no device could be opened.
    bool audio_enabled = true;
};

} // namespace rv_3dmppc
