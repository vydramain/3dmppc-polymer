#pragma once

#include "ca/rv_ca.hpp"
#include "rv_cd.hpp"
#include "rv_cio.hpp"
#include "rv_cv.hpp"

namespace rv_3dmppc {

// The organizer / façade the console hands a disc at boot. It vends the console's
// subsystem controllers by pointer (the console owns them; the disc only borrows,
// never deletes). A disc holds one rv_pdko* and asks it for a controller rather
// than constructing one. Pure abstract: the concrete console subclasses it.
class rv_pdko {
   public:
    virtual ~rv_pdko() = default;

    virtual rv_ca*  audio() = 0;  // sound chip (low-level SPU)
    virtual rv_cd*  drive() = 0;  // disc drive — reads the mounted .mppcdisc
    virtual rv_cio* io()    = 0;  // gamepads + memory card
    virtual rv_cv*  video() = 0;  // GPU / rasterizer
};

}  // namespace rv_3dmppc
