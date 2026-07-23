#pragma once

#include "pdk/rv_pdko.hpp"
#include "rv_pconsole/ca/rv_pcca.hpp"
#include "rv_pconsole/cd/rv_pccd.hpp"
#include "rv_pconsole/cio/rv_pccio.hpp"
#include "rv_pconsole/cm/rv_pccm.hpp"
#include "rv_pconsole/cv/rv_pccv.hpp"
#include "rv_pconsole/rv_pconsole_conf.hpp"

namespace rv_3dmppc {

class rv_pconsole : public rv_pdko {
   private:
    rv_pcca ca_;
    rv_pccd cd_;
    rv_pccio cio_;
    rv_pccm cm_;
    rv_pccv cv_;

   public:
    rv_pconsole(const rv_pconsole_conf conf)
        : ca_(conf.ca), cio_(conf.cio), cm_(conf.cm), cv_(conf.cv) {}

    ~rv_pconsole() = default;

    rv_ca* ca() override;

    rv_cd* cd() override;

    rv_cm* cm() override;

    rv_cio* cio() override;

    rv_cv* cv() override;
};

}  // namespace rv_3dmppc
