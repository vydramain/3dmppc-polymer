#pragma once

#include <cstdint>

namespace rv_3dmppc {

// Controller Memory card — the writable-medium counterpart of the drive
// (rv_cd): where the disc is read-only, the card is where a game persists its
// saves. The accessor is rv_pdko::card().
//
// The medium is a fixed set of equally-sized SLOTS. Both the slot count and the
// slot size are IMPLEMENTATION-defined — query card_slots() / card_slot_size()
// in rv_de::disc_initialize and refuse to run (negative rv_err) if the disc's
// save blob does not fit; the reference console's defaults live in
// docs/platform/specs.md. The card is always inserted: persistence (the
// file-backed image, where it lives, when it is flushed) is the console's
// concern, never an operation the game invokes.
//
// A slot is read and written WHOLE, into/from memory the game owns; the bytes
// are copied during the call. Methods return >= 0 on success, a negative
// rv_err on failure.
class rv_cm {
   public:
    virtual ~rv_cm() = default;

    // Number of slots this console provides (stable for the whole session —
    // query once at disc_initialize). Returns the count (>= 0).
    virtual int64_t card_slots() = 0;

    // Size of one slot, in bytes (every slot is the same size). A game designs
    // its save blob against this at build time — validate it at
    // disc_initialize. Returns the size (>= 0).
    virtual int64_t card_slot_size() = 0;

    // Report how many bytes are stored in `slot`.
    //
    // Returns the size (>= 0), or a negative rv_err:
    //   RV_ERR_INVAL  `slot` is out of range
    //   RV_ERR_NOENT  the slot is empty (no save yet)
    virtual int64_t card_size(int64_t slot) = 0;

    // Copy the whole content of `slot` into the game's buffer.
    //
    // Returns the number of bytes written to `baddr` (>= 0), or a negative
    // rv_err:
    //   RV_ERR_INVAL  `slot` out of range, null `baddr`, or `baddr_size`
    //                 smaller than the stored content
    //   RV_ERR_NOENT  the slot is empty (a fresh card reads as all-empty)
    //   RV_ERR_IO     the backing medium failed
    virtual int64_t card_read(int64_t slot, void* baddr, int64_t baddr_size) = 0;

    // Replace the whole content of `slot` with `data_size` bytes of `data`.
    //
    // ATOMIC: on success the slot holds exactly the new bytes; on any failure —
    // including RV_ERR_IO — the previous content of the slot is left intact.
    // This is the promise that lets a save survive a mid-write crash.
    //
    // Returns RV_OK, or a negative rv_err:
    //   RV_ERR_INVAL  `slot` out of range, null `data`, `data_size` negative
    //                 or larger than card_slot_size()
    //   RV_ERR_IO     the backing medium failed (old content intact)
    virtual int64_t card_write(int64_t slot, const void* data, int64_t data_size) = 0;

    // Empty `slot`: afterwards card_size()/card_read() report RV_ERR_NOENT.
    //
    // Returns RV_OK, or a negative rv_err:
    //   RV_ERR_INVAL  `slot` is out of range
    //   RV_ERR_IO     the backing medium failed (old content intact)
    virtual int64_t card_erase(int64_t slot) = 0;
};

}  // namespace rv_3dmppc
