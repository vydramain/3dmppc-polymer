#pragma once

#include <cstdint>

namespace rv_3dmppc {

// A block of sample data the game hands to rv_ca::sound_asset_write() for upload
// into sound RAM. Plain POD: it only *borrows* the bytes (the game owns them) and
// carries its own length, so the upload call needs no separate size argument.
//
// The borrow lasts for the duration of the call; the bytes are copied into sound
// RAM and the game may release its buffer afterwards.
//
// Format is raw PCM for now; ADPCM encoding is DEFERRED (see README / rv_ca).
struct rv_sample {
    const void* data;  // sample bytes, owned by the game (main RAM)
    int64_t     size;  // length of `data`, in bytes
};

}  // namespace rv_3dmppc
