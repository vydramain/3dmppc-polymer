#pragma once

#include "pdk/ca/rv_ca.hpp"
#include "pdk/cd/rv_cd.hpp"
#include "pdk/cio/rv_cio.hpp"
#include "pdk/cm/rv_cm.hpp"
#include "pdk/cv/rv_cv.hpp"
#include "pdk/rv_err.hpp"  // IWYU pragma: keep (shared error vocabulary)

namespace rv_3dmppc {

// The organizer / facade the console hands a disc at boot. It vends the console's
// subsystem controllers by pointer (the console owns them; the disc only borrows,
// never deletes). A disc holds one rv_pdko* and asks it for a controller rather
// than constructing one. Pure abstract: the concrete console subclasses it.
//
// Every pointer vended here is borrowed and stays valid until the console tears
// the disc down.
class rv_pdko {
   public:
    virtual ~rv_pdko() = default;

    virtual rv_ca* ca() = 0;    // sound chip (low-level SPU)
    virtual rv_cd* cd() = 0;    // disc drive - reads the mounted .mppcdisc
    virtual rv_cm* cm() = 0;    // memory card - persistent save slots
    virtual rv_cio* cio() = 0;  // gamepads + haptic output + mouse
    virtual rv_cv* cv() = 0;    // GPU / rasterizer
};

}  // namespace rv_3dmppc
