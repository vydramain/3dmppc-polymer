#pragma once

#include "pdk/cio/rv_imouse.h"
#include "pdk/cio/rv_isource.h"
#include "pdk/cio/rv_ohaptic.h"
#include "rv_pconsole/cio/rv_pccio.hpp"
#include "rv_pconsole/rv_pconsole_conf.hpp"

namespace rv_3dmppc
{

class rv_pccio_null final : public rv_pccio
{
public:
    explicit rv_pccio_null(const rv_pccio_conf &conf);

    int64_t iport_count() override;

    uint64_t iport_abilities(int64_t) override;

    rv_imouse imouse() override;

    rv_istate iport_state(int64_t) override;

    int64_t ohaptic(int64_t, rv_oheffect) override;

    bool valid() const override { return true; }

private:
    rv_pccio_conf conf_;
};

} // namespace rv_3dmppc
