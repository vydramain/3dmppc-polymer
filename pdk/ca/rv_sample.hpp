#pragma once

namespace rv_3dmppc {

// A block of sample data the game hands to rv_ca::write() for upload into sound
// RAM. Plain POD: it only *borrows* the bytes (the game owns them) and carries
// its own length, so write() needs no separate size argument.
//
// Format is raw PCM for now; ADPCM encoding is DEFERRED (see README / rv_ca).
struct rv_sample {
    const void* data = nullptr;  // sample bytes, owned by the game (main RAM)
    long        size = 0;        // length of `data`, in bytes
};

}  // namespace rv_3dmppc
