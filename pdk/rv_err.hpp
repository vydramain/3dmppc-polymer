#pragma once

// Shared error convention for every PDK controller.
//
// Kernel-style: a call returns >= 0 on success and a negative rv_err on failure.
// Calls that yield a value (e.g. rv_ca::sound_asset_malloc returns a sound-RAM
// address, rv_cd::asset_read returns a byte count) return that value when >= 0,
// or one of the negative codes below. Callers test uniformly with
// `if (rc < 0) { ... }`.
//
// A bitmask returned through this channel uses bits 0..62, so a valid mask is
// never negative.
//
// These values are ABI: existing codes keep their number, new codes are appended.
namespace rv_3dmppc {

enum rv_err : int {
    RV_OK        = 0,
    RV_ERR_INVAL = -1,  // malformed call: bad argument, unknown handle, short buffer
    RV_ERR_NOMEM = -2,  // a pool the controller manages is exhausted
    RV_ERR_BUSY  = -3,  // the resource is occupied; retrying later may succeed
    RV_ERR_NOENT = -4,  // the named thing does not exist
    RV_ERR_IO    = -5,  // the device failed to carry out a well-formed call
};

}  // namespace rv_3dmppc
