#include "rv_pconsole.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

#include "pdk/rv_err.h"
#include "pdklib/rv_logs/rv_logs.hpp"
#include "rv_pconsole/platform/rv_pcplatform.hpp"
#include "rv_pconsole/rv_pcslots.hpp"

// The console's PCM format and the platform's are the same format by
// construction (both are fixed, not negotiated), but they are declared in
// two different files for two different reasons, so a silent drift between
// them must fail the build rather than the ear.
static_assert(rv_3dmppc::RV_PCCA_PCM_RATE == rv_3dmppc::RV_PCPLATFORM_PCM_RATE,
    "the SPU's sample rate and the platform's PCM sink must agree");
static_assert(rv_3dmppc::RV_PCCA_PCM_CHANNELS == rv_3dmppc::RV_PCPLATFORM_PCM_CHANNELS,
    "the mixer's channel count and the platform's PCM sink must agree");

namespace
{

// Slot names: the boot journal and check_ready() both print them, so they are written once.
inline constexpr const char *rv_pcslot_ca = "ca";
inline constexpr const char *rv_pcslot_cio = "cio";
inline constexpr const char *rv_pcslot_cm = "cm";
inline constexpr const char *rv_pcslot_cv = "cv";
inline constexpr const char *rv_pcslot_cg = "cg";
inline constexpr const char *rv_pcslot_cd = "cd";
inline constexpr const char *rv_pcslot_cl = "cl";

// Console text ids of a controller that did not start: the found and the recommended line.
inline constexpr const char *rv_pcboot_text_not_started = "boot.not_started";
inline constexpr const char *rv_pcboot_text_see_log = "boot.see_log";

// Text ids of a refused platform service, per rv_pcboot_refusal_rows below.
inline constexpr const char *rv_pcboot_text_audio_found = "boot.audio_found";
inline constexpr const char *rv_pcboot_text_audio_recommend = "boot.audio_recommend";
inline constexpr const char *rv_pcboot_text_gamepads_found = "boot.gamepads_found";
inline constexpr const char *rv_pcboot_text_gamepads_recommend = "boot.gamepads_recommend";
inline constexpr const char *rv_pcboot_text_motion_found = "boot.motion_found";
inline constexpr const char *rv_pcboot_text_motion_recommend = "boot.motion_recommend";
inline constexpr const char *rv_pcboot_text_video_found = "boot.video_found";
inline constexpr const char *rv_pcboot_text_video_recommend = "boot.video_recommend";

// A row warns its slot when its refused bit is set. Rows are in report order: the first
// matching row for a slot gives its texts. The caller passes only the bits its slot reads.
struct rv_pcboot_refusal_row {
    const char *slot;
    uint32_t refused_bit;
    const char *found_id;
    const char *recommend_id;
};

inline constexpr rv_pcboot_refusal_row rv_pcboot_refusal_rows[] = {
    { rv_pcslot_ca, rv_3dmppc::RV_PCPLATFORM_REFUSED_AUDIO, rv_pcboot_text_audio_found, rv_pcboot_text_audio_recommend },
    { rv_pcslot_cio,
        rv_3dmppc::RV_PCPLATFORM_REFUSED_GAMEPADS,
        rv_pcboot_text_gamepads_found,
        rv_pcboot_text_gamepads_recommend },
    { rv_pcslot_cio,
        rv_3dmppc::RV_PCPLATFORM_REFUSED_PAD_MOTION,
        rv_pcboot_text_motion_found,
        rv_pcboot_text_motion_recommend },
    { rv_pcslot_cv, rv_3dmppc::RV_PCPLATFORM_REFUSED_VIDEO, rv_pcboot_text_video_found, rv_pcboot_text_video_recommend },
};

// Journals a controller right after it is made. Its own constructor has already logged why it refused.
void rv_pcboot_journal_slot(rv_3dmppc::rv_pcboot_journal &journal, const char *name, bool valid, uint32_t refused)
{
    if (!valid) {
        journal.add_warning(name, rv_pcboot_text_not_started, rv_pcboot_text_see_log);
        return;
    }
    for (const auto &row : rv_pcboot_refusal_rows) {
        if (std::string_view(row.slot) == name && (refused & row.refused_bit) != 0) {
            journal.add_warning(name, row.found_id, row.recommend_id);
            return;
        }
    }
    journal.add_ok(name);
}

} // namespace

rv_3dmppc::rv_pconsole::rv_pconsole(const rv_3dmppc::rv_pconsole_conf &conf,
    rv_3dmppc::rv_pcplatform &platform,
    rv_3dmppc::rv_pcloader *loader)
    : params_(conf.params)
    , platform_(platform)
    , loader_(loader)
    , pcm_(static_cast<size_t>(
          (RV_PCCA_PCM_RATE / static_cast<int64_t>(params_.target_fps ? params_.target_fps : RV_PCONSOLE_DEFAULT_TARGET_FPS) +
              1) *
          RV_PCCA_PCM_CHANNELS))
    , script_entry_(conf.cl.script_entry)
{
    // What the platform refused to bring up, read once: the warnings come from the same bits.
    const uint32_t refused = platform_.refused();

    // Built one at a time, each journaled as it is made, in dependency order:
    // cg_ needs cv_, cl_ needs cd_.
    ca_ = rv_pcca_make(conf.slots.ca, conf.ca);
    rv_pcboot_journal_slot(boot_journal_, rv_pcslot_ca, ca_->valid(), refused);
    // Only the standard cio reads the platform's pads; the null cio never does.
    const uint32_t cio_refused = conf.slots.cio == rv_pccio_impl::standard ? refused : 0;
    cio_ = rv_pccio_make(conf.slots.cio, conf.cio, platform_);
    rv_pcboot_journal_slot(boot_journal_, rv_pcslot_cio, cio_->valid(), cio_refused);
    cm_ = rv_pccm_make(conf.slots.cm, conf.cm);
    rv_pcboot_journal_slot(boot_journal_, rv_pcslot_cm, cm_->valid(), refused);
    cv_ = rv_pccv_make(conf.slots.cv, conf.cv);
    rv_pcboot_journal_slot(boot_journal_, rv_pcslot_cv, cv_->valid(), refused);
    boot_screen_during_boot();
    cg_ = rv_pccg_make(conf.slots.cg, conf.cg, *cv_);
    rv_pcboot_journal_slot(boot_journal_, rv_pcslot_cg, cg_->valid(), refused);
    boot_screen_during_boot();
    cd_ = rv_pccd_make(conf.slots.cd, conf.cd);
    rv_pcboot_journal_slot(boot_journal_, rv_pcslot_cd, cd_->valid(), refused);
    boot_screen_during_boot();
    cl_ = rv_pccl_make(conf.slots.cl, conf.cl, *cd_);
    rv_pcboot_journal_slot(boot_journal_, rv_pcslot_cl, cl_->valid(), refused);
    boot_screen_during_boot();

    // The attach calls run once all seven controllers exist: cd_ attaches cv_, ca_
    // and cg_. cv_ must outlive cd_ (rv_pccd.hpp: ~rv_pccd_fs frees textures
    // through it). audio_attach() is the same seam for ca_ - see rv_pccd.hpp.
    cd_->video_attach(*cv_);
    cd_->audio_attach(*ca_);
    cd_->geometry_attach(*cg_);
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
rv_cg *rv_3dmppc::rv_pconsole::cg()
{
    return reinterpret_cast<rv_cg *>(cg_.get());
}
rv_cl *rv_3dmppc::rv_pconsole::cl()
{
    return reinterpret_cast<rv_cl *>(cl_.get());
}

int rv_3dmppc::rv_pconsole::check_ready() const
{
    // The reason a controller is not usable was logged by its constructor; this names
    // which one refused the start. The first one in this order is the one reported.
    // code: what the refusal means. Memory reservations fail with RV_ERR_NOMEM; the card
    // image is a file the console reads, so a bad one is a device failure, RV_ERR_IO.
    struct slot_check {
        const char *name;
        bool valid;
        int code;
    };
    const slot_check slots[] = {
        { rv_pcslot_ca, ca_->valid(), RV_ERR_NOMEM },
        { rv_pcslot_cd, cd_->valid(), RV_ERR_NOMEM },
        { rv_pcslot_cio, cio_->valid(), RV_ERR_NOMEM },
        { rv_pcslot_cm, cm_->valid(), RV_ERR_IO },
        { rv_pcslot_cv, cv_->valid(), RV_ERR_NOMEM },
        { rv_pcslot_cg, cg_->valid(), RV_ERR_NOMEM },
        { rv_pcslot_cl, cl_->valid(), RV_ERR_NOMEM },
    };
    for (const auto &slot : slots) {
        if (!slot.valid) {
            RV_LOG_ERR("pconsole", "controller '{}' is not usable, the console does not start", slot.name);
            return slot.code;
        }
    }
    return RV_OK;
}

const rv_3dmppc::rv_pcboot_journal &rv_3dmppc::rv_pconsole::boot_journal() const
{
    return boot_journal_;
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
extern "C" rv_cg *rv_pdko_cg(rv_pdko *o)
{
    return reinterpret_cast<rv_3dmppc::rv_pconsole *>(o)->cg();
}
