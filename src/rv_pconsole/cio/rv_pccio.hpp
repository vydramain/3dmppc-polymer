#pragma once

#include "pdk/cio/rv_cio.hpp"
#include "rv_pconsole/rv_pconsole_conf.hpp"

namespace rv_3dmppc {
class rv_pccio : public rv_cio {
   private:
    rv_pccio_conf conf_;

   public:
    rv_pccio(const rv_pccio_conf conf) : conf_(conf) {}
    ~rv_pccio() = default;

    int64_t iport_count() override;

    uint64_t iport_abilities(int64_t port) override;

    rv_imouse imouse() override;

    rv_istate iport_state(int64_t port) override;

    int64_t ohaptic(int64_t port, rv_oheffect effect) override;
};

}  // namespace rv_3dmppc
