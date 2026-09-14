#include "rv_pboot_check.hpp"

#include <cstdint>

#include "pdk/rv_err.h"
#include "pdklib/rv_logs/rv_logs.hpp"
#include "rv_pconsole/rv_pcbudget.hpp"
#include "rv_pconsole/rv_pcslots.hpp"

namespace rv_3dmppc
{

namespace
{

// A voice mask is an int64_t carrying bits 0..62 (pdk/ca/rv_ca.h), so 63 is
// the most voices any console can ever name — see RV_PCCA_MAX_VOICES in
// rv_pcca.cpp, whose clamp to this same limit is the unreachable backstop.
constexpr int64_t RV_PCCA_MAX_VOICES = 63;

// A field must be strictly positive when its subsystem is `active`. A
// negative value is malformed regardless — an unset (zero) field of a
// switched-off subsystem is the only value this passes without `active`.
bool bad_field(const char *field, int64_t value, bool active)
{
    if (value < 0) {
        RV_LOG_ERR("pccheck", "'{}' is negative ({})", field, value);
        return true;
    }
    if (active && value == 0) {
        RV_LOG_ERR("pccheck", "'{}' must be positive, this subsystem is on", field);
        return true;
    }
    return false;
}

// Find the row in `table` whose impl matches `impl`, call its evaluate(),
// log the answer, and fold it into `total`. Returns RV_OK or RV_ERR_INVAL —
// from the class's own refusal, or from an overflowing sum.
template <typename Table, typename Impl>
int64_t evaluate_slot(const Table &table, Impl impl, const char *slot,
    const rv_pdklib::rv_manifest_budget &budget, int64_t &total)
{
    for (const auto &row : table) {
        if (row.impl != impl) {
            continue;
        }

        int64_t bytes = 0;
        const int64_t rc = row.evaluate(budget, bytes);
        if (rc != RV_OK) {
            return rc;
        }

        RV_LOG_INFO("pccheck", "{}={}: {} byte(s)", slot, row.name, bytes);
        return rv_pcbudget_add(slot, total, bytes) ? RV_ERR_INVAL : RV_OK;
    }

    // Every rv_pcslots field is one of that table's enumerators — there is no
    // impl a row does not name.
    return RV_ERR_INVAL;
}

} // namespace

// The budget is stated in console units (a disc must look the same on every
// backend); what those units cost in host bytes is entirely up to the
// concrete slot classes `slots` names, evaluated below before any of them
// exists.
int64_t rv_pboot_check_budget(
    const rv_pdklib::rv_manifest_budget &budget,
    const rv_pcslots &slots,
    const rv_pboot_mode_info &machine)
{
    // Sanity of the declared numbers, identical for every mode and backend:
    // the rasterizer's own memory (cv.*) is required whether cv=null or not
    // (nothing here looks at display bounds), pccio has no on/off switch, and
    // pcca is always active — a run with no audio device still declares (and
    // is charged for) the sound RAM and voices its disc asked for.
    if (bad_field("budget.pcca.voice_count", budget.pcca.voice_count, true) ||
        bad_field("budget.pcca.sound_memory_size", budget.pcca.sound_memory_size, true) ||
        bad_field("budget.pccv.screen_width", budget.pccv.screen_width, true) ||
        bad_field("budget.pccv.screen_height", budget.pccv.screen_height, true) ||
        bad_field("budget.pccv.texture_max_width", budget.pccv.texture_max_width, true) ||
        bad_field("budget.pccv.texture_max_height", budget.pccv.texture_max_height, true) ||
        bad_field("budget.pccv.video_memory_size", budget.pccv.video_memory_size, true) ||
        bad_field("budget.pccv.frame_capacity", budget.pccv.frame_capacity, true) ||
        bad_field("budget.pccv.ot_bucket_count", budget.pccv.ot_bucket_count, true) ||
        bad_field("budget.pccio.iport_count", budget.pccio.iport_count, true) ||
        bad_field("budget.pccm.card_slots", budget.pccm.card_slots, true) ||
        bad_field("budget.pccm.card_slot_size", budget.pccm.card_slot_size, true) ||
        bad_field("budget.pccl.script_memory_size", budget.pccl.script_memory_size, false)) {
        return RV_ERR_INVAL;
    }

    // voice_count is never silently reduced: either the mask can name every
    // requested voice, or the run is refused by name here — rv_pcca.cpp's own
    // clamp to RV_PCCA_MAX_VOICES must never actually fire.
    if (budget.pcca.voice_count > RV_PCCA_MAX_VOICES) {
        RV_LOG_ERR("pccheck",
            "'budget.pcca.voice_count' asks for {}, over the {} this console can name "
            "(a voice mask carries bits 0..62)",
            budget.pcca.voice_count, RV_PCCA_MAX_VOICES);
        return RV_ERR_INVAL;
    }

    // Every field above is now known non-negative (and positive where
    // required), so every evaluate() below may assume that too.
    int64_t total = 0;
    int64_t rc = RV_OK;
    if ((rc = evaluate_slot(RV_PCSLOTS_CA, slots.ca, "ca", budget, total)) < 0 ||
        (rc = evaluate_slot(RV_PCSLOTS_CV, slots.cv, "cv", budget, total)) < 0 ||
        (rc = evaluate_slot(RV_PCSLOTS_CIO, slots.cio, "cio", budget, total)) < 0 ||
        (rc = evaluate_slot(RV_PCSLOTS_CL, slots.cl, "cl", budget, total)) < 0 ||
        (rc = evaluate_slot(RV_PCSLOTS_CD, slots.cd, "cd", budget, total)) < 0 ||
        (rc = evaluate_slot(RV_PCSLOTS_CM, slots.cm, "cm", budget, total)) < 0) {
        return rc;
    }

    // Compare against what the machine actually has.
    if (machine.ram_available < 0) {
        RV_LOG_ERR("pccheck",
            "machine RAM unknown, cannot show disc's {} byte(s) fit", total);
        return RV_ERR_INVAL;
    }

    if (total > machine.ram_available) {
        RV_LOG_ERR("pccheck", "disc needs {} byte(s) of RAM, this machine has {}", total,
            machine.ram_available);
        return RV_ERR_INVAL;
    }

    return RV_OK;
}

} // namespace rv_3dmppc
