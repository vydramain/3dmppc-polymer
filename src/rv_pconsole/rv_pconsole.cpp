#include "rv_pconsole.hpp"

#include <memory>
#include <string>

#include "pdk/rv_err.h"
#include "pdklib/rv_logs/rv_logs.hpp"
#include "rv_pconsole/rv_pcslots.hpp"

// The console's PCM format and the platform's are the same format by
// construction (both are fixed, not negotiated), but they are declared in
// two different files for two different reasons, so a silent drift between
// them must fail the build rather than the ear.
static_assert(rv_3dmppc::RV_PCCA_PCM_RATE == rv_3dmppc::RV_PCPLATFORM_PCM_RATE,
    "the SPU's sample rate and the platform's PCM sink must agree");
static_assert(rv_3dmppc::RV_PCCA_PCM_CHANNELS == rv_3dmppc::RV_PCPLATFORM_PCM_CHANNELS,
    "the mixer's channel count and the platform's PCM sink must agree");


rv_3dmppc::rv_pconsole::rv_pconsole(const rv_3dmppc::rv_pconsole_conf &conf,
    rv_3dmppc::rv_pcplatform &platform, rv_3dmppc::rv_pcloader *loader)
    : params_(conf.params)
    , platform_(platform)
    , ca_(rv_pcca_make(conf.slots.ca, conf.ca))
    , cio_(rv_pccio_make(conf.slots.cio, conf.cio, platform_))
    , cm_(rv_pccm_make(conf.slots.cm, conf.cm))
    , cv_(rv_pccv_make(conf.slots.cv, conf.cv))
    , cd_(rv_pccd_make(conf.slots.cd, conf.cd))
    , cl_(rv_pccl_make(conf.slots.cl, conf.cl, *cd_))
    , loader_(loader)
    , pcm_(static_cast<size_t>(
          (RV_PCCA_PCM_RATE / static_cast<int64_t>(params_.target_fps ? params_.target_fps : 60) + 1) *
          RV_PCCA_PCM_CHANNELS))
    , script_entry_(conf.cl.script_entry)
{
    // Here and not in the initialiser list: video_attach() is not itself part
    // of building cd_, and keeping it out of the list means the invariant it
    // establishes (rv_pccd.hpp: cv_ outlives cd_) is not tangled with the
    // ORDER the list happens to be written in.
    cd_->video_attach(*cv_);
}

// This is where the contract meets the machine. reinterpret_cast is mandatory
// here, not a style choice: rv_ca/rv_cv/... are incomplete to C++, so
// static_cast from or to them cannot compile. Every line hands out the slot's
// BASE address (ca_.get(), cd_.get(), ...); the extern "C" block in each
// XX/rv_pcXX.cpp casts back to that same base (one of several such cast
// sites, not the only one - see the block below).
rv_ca *rv_3dmppc::rv_pconsole::ca()
{
    return reinterpret_cast<rv_ca *>(ca_.get());
}
rv_cd *rv_3dmppc::rv_pconsole::cd()
{
    return reinterpret_cast<rv_cd *>(cd_.get());
}
rv_cm *rv_3dmppc::rv_pconsole::cm()
{
    return reinterpret_cast<rv_cm *>(cm_.get());
}
rv_cio *rv_3dmppc::rv_pconsole::cio()
{
    return reinterpret_cast<rv_cio *>(cio_.get());
}
rv_cv *rv_3dmppc::rv_pconsole::cv()
{
    return reinterpret_cast<rv_cv *>(cv_.get());
}
rv_cl *rv_3dmppc::rv_pconsole::cl()
{
    return reinterpret_cast<rv_cl *>(cl_.get());
}

bool rv_3dmppc::rv_pconsole::ready() const
{
    return ca_->valid() && cd_->valid() && cio_->valid() && cm_->valid() && cv_->valid() && cl_->valid();
}

// --- C contract (pdk/rv_pdko.h) ----------------------------------------------
// The facade through which a disc sees the machine. An rv_pdko* handle is the
// address of an rv_pconsole: there is one console in the process, and it is the
// only implementation of the facade.
//
// Each of these returns the address of a controller field cast to the contract's
// opaque type. The reverse cast lives in rv_pcXX.cpp next to the controller
// itself, so both ends of every pair are visible in their own file.

extern "C" rv_ca *rv_pdko_ca(rv_pdko *o)
{
    return reinterpret_cast<rv_3dmppc::rv_pconsole *>(o)->ca();
}
extern "C" rv_cd *rv_pdko_cd(rv_pdko *o)
{
    return reinterpret_cast<rv_3dmppc::rv_pconsole *>(o)->cd();
}
extern "C" rv_cio *rv_pdko_cio(rv_pdko *o)
{
    return reinterpret_cast<rv_3dmppc::rv_pconsole *>(o)->cio();
}
extern "C" rv_cl *rv_pdko_cl(rv_pdko *o)
{
    return reinterpret_cast<rv_3dmppc::rv_pconsole *>(o)->cl();
}
extern "C" rv_cm *rv_pdko_cm(rv_pdko *o)
{
    return reinterpret_cast<rv_3dmppc::rv_pconsole *>(o)->cm();
}
extern "C" rv_cv *rv_pdko_cv(rv_pdko *o)
{
    return reinterpret_cast<rv_3dmppc::rv_pconsole *>(o)->cv();
}
