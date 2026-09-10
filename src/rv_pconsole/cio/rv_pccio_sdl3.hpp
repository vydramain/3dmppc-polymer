#pragma once

#include "pdk/cio/rv_imouse.h"
#include "pdk/cio/rv_isource.h"
#include "pdk/cio/rv_ohaptic.h"
#include "rv_pconsole/cio/rv_pccio.hpp"
#include "rv_pconsole/rv_pconsole_conf.hpp"

namespace rv_3dmppc
{

class rv_pchost_sdl3;

class rv_pccio_sdl3 final : public rv_pccio
{
public:
    // `host` is borrowed: the console owns it and outlives every controller it
    // hands to a disc.
    rv_pccio_sdl3(const rv_pccio_conf &conf, rv_pchost_sdl3 &host)
        : conf_(conf)
        , host_(host)
    {
    }

    int64_t iport_count() override;

    uint64_t iport_abilities(int64_t port) override;

    rv_imouse imouse() override;

    rv_istate iport_state(int64_t port) override;

    int64_t ohaptic(int64_t port, rv_oheffect effect) override;

    bool valid() const override { return true; }

private:
    // True when `port` names one of the console's fixed slots. Out of range is
    // NOT an error for the query methods (rv_cio.hpp): they report zeroes.
    bool port_in_range(int64_t port) const
    {
        return port >= 0 && port < conf_.iport_count;
    }

    rv_pccio_conf conf_;
    rv_pchost_sdl3 &host_;
};

} // namespace rv_3dmppc
