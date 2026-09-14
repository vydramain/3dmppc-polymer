// The console's rv_cio contract. Pure virtual interface.
//
// PATTERN: adapter. The platform speaks "devices" (a window's keyboard and
// mouse, physical pads by id); rv_cio speaks "the contract" (stable ports,
// data-not-status queries, a single error channel). rv_pccio_std is the seam
// between the two vocabularies.
#pragma once

#include "pdk/cio/rv_imouse.h"
#include "pdk/cio/rv_isource.h"
#include "pdk/cio/rv_ohaptic.h"

namespace rv_3dmppc
{

class rv_pccio
{
public:
    virtual ~rv_pccio() = default;

    rv_pccio(const rv_pccio &) = delete;
    rv_pccio &operator=(const rv_pccio &) = delete;

    virtual int64_t iport_count() = 0;

    virtual uint64_t iport_abilities(int64_t port) = 0;

    virtual rv_imouse imouse() = 0;

    virtual rv_istate iport_state(int64_t port) = 0;

    virtual int64_t ohaptic(int64_t port, rv_oheffect effect) = 0;

    virtual bool valid() const = 0;

protected:
    rv_pccio() = default;
};

} // namespace rv_3dmppc
