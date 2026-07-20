#pragma once

#include <cstdint>

#include "../rv_err.hpp"

namespace rv_3dmppc {

// Controller Drive — reads entries from the mounted .mppcdisc medium (the
// accessor is rv_pdko::drive()). The disc is the medium; the drive is what reads
// it.
//
// The medium is READ-ONLY. Persistent save is the memory card — see rv_cio.
//
// An entry is addressed by NAME, resolved once into a HANDLE and read by handle
// from then on. The drive never allocates the game's data buffer: asset_read()
// copies into memory the game owns.
//
// Methods return >= 0 on success, a negative rv_err on failure.
//
// DEFERRED: enumerating entries, ranged reads, streaming.
class rv_cd {
   public:
    virtual ~rv_cd() = default;

    // Resolve a resource name into a handle.
    //
    // `resname` names one entry in the medium's asset set: a plain name with no
    // path separators. It is case-sensitive.
    //
    // Returns the handle (>= 0), or a negative rv_err:
    //   RV_ERR_INVAL  `resname` is null, empty, or contains a path separator
    //   RV_ERR_NOENT  no such entry on the medium
    //   RV_ERR_NOMEM  the resource table cannot grow
    //
    // The drive copies the name: `resname` need not outlive the call.
    //
    // The same name always resolves to the same handle. A handle stays valid
    // while the medium is mounted and needs no release. May be called at any
    // time; resolving at boot makes a missing entry fail on the loading screen
    // instead of during play.
    virtual int64_t asset_open(const char* resname) = 0;

    // Report the size in bytes of the entry behind `handle`.
    //
    // Returns the size (>= 0), or a negative rv_err:
    //   RV_ERR_INVAL  unknown handle
    //   RV_ERR_NOENT  the entry has disappeared since asset_open()
    //   RV_ERR_IO     the entry cannot be measured
    //
    // The size is a hint for sizing an allocation, not a guarantee: an entry may
    // change between this call and the read that follows. The authoritative count
    // is what asset_read() returns.
    virtual int64_t asset_size(int64_t handle) = 0;

    // Copy the whole entry behind `handle` into the game's buffer.
    //
    // `baddr` is memory the game owns; `baddr_size` is its capacity in bytes.
    // `baddr` must be non-null even when the entry is empty.
    //
    // Returns the number of bytes written (>= 0), or a negative rv_err:
    //   RV_ERR_INVAL  unknown handle, null `baddr`, negative `baddr_size`, or
    //                 `baddr_size` < entry size
    //   RV_ERR_NOENT  the entry has disappeared since asset_open()
    //   RV_ERR_IO     the read failed
    //
    // The entry is read whole; there are no partial reads. On failure nothing is
    // written to `baddr`.
    virtual int64_t asset_read(int64_t handle, void* baddr, int64_t baddr_size) = 0;
};

}  // namespace rv_3dmppc
