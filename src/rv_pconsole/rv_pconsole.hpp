#pragma once

#include "pdk/de/rv_de.hpp"
#include "pdk/rv_pdko.hpp"
#include "rv_pconsole/ca/rv_pcca.hpp"
#include "rv_pconsole/cd/rv_pccd.hpp"
#include "rv_pconsole/cio/rv_pccio.hpp"
#include "rv_pconsole/cm/rv_pccm.hpp"
#include "rv_pconsole/cv/rv_pccv.hpp"
#include "rv_pconsole/rv_pchost.hpp"
#include "rv_pconsole/rv_pconsole_conf.hpp"

namespace rv_3dmppc {

// PATTERN: composition root. This is the single place where the concrete
// machine is assembled — the host, the five controllers, and the geometry they
// were built from. Nothing below constructs a subsystem: a controller receives
// what it needs and never reaches sideways for it.
class rv_pconsole : public rv_pdko {
   private:
    rv_pconsole_params params_;

    // NEUROSLOP-BEGIN (claude-opus-5)
    // DECLARATION ORDER IS LOAD-BEARING: the host owns SDL and is borrowed by
    // cio_ and cv_, so it must be constructed before them and torn down after.
    rv_pchost host_;
    // NEUROSLOP-END

    rv_pcca ca_;
    rv_pccd cd_;
    rv_pccio cio_;
    rv_pccm cm_;
    rv_pccv cv_;

   public:
    explicit rv_pconsole(const rv_pconsole_conf& conf);

    ~rv_pconsole() = default;

    rv_ca* ca() override;
    rv_cd* cd() override;
    rv_cio* cio() override;
    rv_cm* cm() override;
    rv_cv* cv() override;

    // rv_de& disc_load(const char* path);

    // NEUROSLOP-BEGIN (claude-opus-5)
    // PATTERN: inversion of control. The frame loop belongs to the console; the
    // disc lives inside the rv_de hooks and never owns a loop of its own.
    //
    // Returns RV_OK when the run ends normally (the disc asked to stop, the
    // frame budget ran out, or the user powered the machine off), or a negative
    // rv_err if the disc refused to initialize or the display would not come up.
    int64_t disc_run(rv_de& disc);
    // NEUROSLOP-END
};

}  // namespace rv_3dmppc
