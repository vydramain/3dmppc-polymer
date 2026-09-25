// The development console's embedded screen (editor/docs/adr/0006-game-frame.md):
// finished frames go into a shared memory object an embedding program created and
// passed as --frame-fd, pad buttons arrive over the dev channel, and no window
// opens. Sound and gamepads stay the wrapped platform's.
#pragma once

#include <cstdint>
#include <memory>

#include "rv_pconsole/platform/rv_pcplatform.hpp"

namespace rv_3dmppc
{

// The memory, native byte order: a header, then RV_PCFRAME_SLOTS slots of
// `slot_bytes` each, slot k at RV_PCFRAME_HEADER_BYTES + k * slot_bytes, its
// pixels RV_PCFRAME_SLOT_HEADER_BYTES after that. The console writes a slot that
// is not `latest`, with `seq` odd while it writes, then publishes it as
// `latest`. A reader copies slot `latest` and keeps the copy only when `seq`
// was even and the same before and after; it never makes the console wait.
inline constexpr uint32_t RV_PCFRAME_MAGIC = 0x42465652; // "RVFB"
inline constexpr uint32_t RV_PCFRAME_VERSION = 1;
inline constexpr uint32_t RV_PCFRAME_SLOTS = 3;
inline constexpr uint32_t RV_PCFRAME_FORMAT_ARGB8888 = 1;
inline constexpr uint32_t RV_PCFRAME_NONE = 0xFFFFFFFFu;
inline constexpr uint64_t RV_PCFRAME_HEADER_BYTES = 64;
inline constexpr uint64_t RV_PCFRAME_SLOT_HEADER_BYTES = 32;

struct rv_pcframe_header {
    uint32_t magic;       // RV_PCFRAME_MAGIC
    uint32_t version;     // RV_PCFRAME_VERSION
    uint32_t slot_count;  // RV_PCFRAME_SLOTS
    uint32_t slot_bytes;  // slot header + pixels, a multiple of 64
    uint32_t latest;      // slot of the last finished frame, RV_PCFRAME_NONE before one
    uint32_t reserved[11];
};
static_assert(sizeof(rv_pcframe_header) == RV_PCFRAME_HEADER_BYTES);

struct rv_pcframe_slot {
    uint32_t seq;         // odd while the console writes this slot
    uint32_t format;      // RV_PCFRAME_FORMAT_ARGB8888: 0xAARRGGBB words
    uint64_t frame;       // pictures written so far, pause pictures included: not the machine's frame
    uint32_t width;
    uint32_t height;
    uint32_t stride;      // bytes per row
    uint32_t reserved;
};
static_assert(sizeof(rv_pcframe_slot) == RV_PCFRAME_SLOT_HEADER_BYTES);

// `inner`, made with no window, wrapped so that window() is the shared memory
// `fd`: open() sizes the object for its frame and maps it. The player build has
// no --frame-fd and its version of this returns `inner` untouched.
std::unique_ptr<rv_pcplatform> rv_pcframe_wrap(std::unique_ptr<rv_pcplatform> inner, int fd);

// The dev channel's side, false for a platform rv_pcframe_wrap did not make.
// Buttons (rv_isource bits) port 0 reads as its keyboard from now on.
bool rv_pcframe_set_pad(rv_pcplatform &platform, uint64_t buttons);
// Slot and count of the last frame written; false before the first as well.
bool rv_pcframe_latest(rv_pcplatform &platform, uint32_t &slot, uint64_t &frame);

} // namespace rv_3dmppc
