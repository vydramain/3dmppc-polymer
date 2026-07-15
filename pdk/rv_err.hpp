#pragma once

// Shared error convention for every PDK controller.
//
// Kernel-style: a call returns >= 0 on success and a negative rv_err on failure.
// Calls that yield a value (e.g. rv_ca::malloc returns a sound-RAM address)
// return that value when >= 0, or one of the negative codes below. Callers test
// uniformly with `if (rc < 0) { ... }`.
namespace rv_3dmppc {

enum rv_err : int {
    RV_OK        = 0,
    RV_ERR_INVAL = -1,  // invalid argument
    RV_ERR_NOMEM = -2,  // out of sound RAM
    RV_ERR_BUSY  = -3,  // no free voice
};

}  // namespace rv_3dmppc
