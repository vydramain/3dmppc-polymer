#include "rv_pboot_check.hpp"

#include <algorithm>
#include <cstdint>
#include <string>

#include "pdk/rv_err.h"
#include "pdklib/rv_logs/rv_logs.hpp"
#include "rv_pconsole/rv_pcbudget.hpp"
#include "rv_pconsole/rv_pcslots.hpp"

namespace rv_3dmppc
{

namespace
{

// A voice mask is an int64_t carrying bits 0..62 (pdk/ca/rv_ca.h), so 63 is
// the most voices any console can ever name - see RV_PCCA_MAX_VOICES in
// rv_pcca.cpp, whose clamp to this same limit is the unreachable backstop.
constexpr int64_t RV_PCCA_MAX_VOICES = 63;

// A field must be strictly positive when its subsystem is `active`. A
// negative value is malformed regardless - an unset (zero) field of a
// switched-off subsystem is the only value this passes without `active`.
int bad_field(const char *field, int64_t value, bool active)
{
    if (value < 0) {
        RV_LOG_ERR("pccheck", "'{}' is negative ({})", field, value);
        return RV_ERR_INVAL;
    }
    if (active && value == 0) {
        RV_LOG_ERR("pccheck", "'{}' must be positive, this subsystem is on", field);
        return RV_ERR_INVAL;
    }
    return RV_OK;
}

// One [budget] field to check; `active` marks a subsystem that must declare it positive.
struct field_check {
    const char *field;
    int64_t value;
    bool active;
};

// Find the row in `table` whose impl matches `impl`, call its evaluate(),
// report a refusal with the slot and implementation, and fold its bytes into
// `total`. Returns RV_OK, or the code the refusing step returned (`error` set).
template <typename Table, typename Impl>
int evaluate_slot(const Table &table,
    Impl impl,
    const char *slot,
    const rv_pdklib::rv_manifest_budget &budget,
    int64_t &total,
    std::string &error)
{
    const auto found = std::ranges::find_if(table, [impl](const auto &row) {
        return row.impl == impl;
    });
    // Every rv_pcslots field is one of that table's enumerators, so this never fires.
    if (found == std::ranges::end(table)) {
        RV_LOG_ERR("pccheck", "{}: no implementation row matches this slot", slot);
        return RV_ERR_INVAL;
    }

    const auto &row = *found;
    int64_t bytes = 0;
    const int eval_rc = row.evaluate(budget, bytes, error);
    if (eval_rc != RV_OK) {
        RV_LOG_ERR("pccheck", "{}={} refuses this budget: {}", slot, row.name, error);
        return eval_rc;
    }

    RV_LOG_INFO("pccheck", "{}={}: {} byte(s)", slot, row.name, bytes);
    const int add_rc = rv_pcbudget_add(total, slot, bytes, error);
    if (add_rc != RV_OK) {
        RV_LOG_ERR("pccheck", "{}={}: {}", slot, row.name, error);
        return add_rc;
    }

    return RV_OK;
}

} // namespace

// The budget is stated in console units (a disc must look the same on every
// backend); what those units cost in host bytes is entirely up to the
// concrete slot classes `slots` names, evaluated below before any of them
// exists.
int rv_pboot_check_budget(const rv_pdklib::rv_manifest_budget &budget,
    const rv_pcslots &slots,
    const rv_pboot_mode_info &machine)
{
    // Sanity of the declared numbers, identical for every mode and backend:
    // the rasterizer's own memory (cv.*) is required whether cv=null or not
    // (nothing here looks at display bounds), pccio has no on/off switch, and
    // pcca is always active - a run with no audio device still declares (and
    // is charged for) the sound RAM and voices its disc asked for.
    const field_check checks[] = {
        { "budget.pcca.voice_count", budget.pcca.voice_count, true },
        { "budget.pcca.sound_memory_size", budget.pcca.sound_memory_size, true },
        { "budget.pccv.screen_width", budget.pccv.screen_width, true },
        { "budget.pccv.screen_height", budget.pccv.screen_height, true },
        { "budget.pccv.texture_max_width", budget.pccv.texture_max_width, true },
        { "budget.pccv.texture_max_height", budget.pccv.texture_max_height, true },
        { "budget.pccv.video_memory_size", budget.pccv.video_memory_size, true },
        { "budget.pccv.frame_capacity", budget.pccv.frame_capacity, true },
        { "budget.pccv.ot_bucket_count", budget.pccv.ot_bucket_count, true },
        { "budget.pccio.iport_count", budget.pccio.iport_count, true },
        { "budget.pccm.card_slots", budget.pccm.card_slots, true },
        { "budget.pccm.card_slot_size", budget.pccm.card_slot_size, true },
        { "budget.pccl.script_memory_size", budget.pccl.script_memory_size, false },
    };
    for (const field_check &check : checks) {
        const int check_rc = bad_field(check.field, check.value, check.active);
        if (check_rc != RV_OK) {
            return check_rc;
        }
    }

    // voice_count is never silently reduced: either the mask can name every
    // requested voice, or the run is refused by name here - rv_pcca.cpp's own
    // clamp to RV_PCCA_MAX_VOICES must never actually fire.
    if (budget.pcca.voice_count > RV_PCCA_MAX_VOICES) {
        RV_LOG_ERR("pccheck",
            "'budget.pcca.voice_count' asks for {}, over the {} this console can name "
            "(a voice mask carries bits 0..62)",
            budget.pcca.voice_count,
            RV_PCCA_MAX_VOICES);
        return RV_ERR_NOMEM;
    }

    // Every field above is now known non-negative (and positive where
    // required), so every evaluate() below may assume that too.
    int64_t total = 0;
    std::string error;

    const int ca_rc = evaluate_slot(RV_PCSLOTS_CA, slots.ca, "ca", budget, total, error);
    if (ca_rc != RV_OK) {
        return ca_rc;
    }

    const int cv_rc = evaluate_slot(RV_PCSLOTS_CV, slots.cv, "cv", budget, total, error);
    if (cv_rc != RV_OK) {
        return cv_rc;
    }

    const int cg_rc = evaluate_slot(RV_PCSLOTS_CG, slots.cg, "cg", budget, total, error);
    if (cg_rc != RV_OK) {
        return cg_rc;
    }

    const int cio_rc = evaluate_slot(RV_PCSLOTS_CIO, slots.cio, "cio", budget, total, error);
    if (cio_rc != RV_OK) {
        return cio_rc;
    }

    const int cl_rc = evaluate_slot(RV_PCSLOTS_CL, slots.cl, "cl", budget, total, error);
    if (cl_rc != RV_OK) {
        return cl_rc;
    }

    const int cd_rc = evaluate_slot(RV_PCSLOTS_CD, slots.cd, "cd", budget, total, error);
    if (cd_rc != RV_OK) {
        return cd_rc;
    }

    const int cm_rc = evaluate_slot(RV_PCSLOTS_CM, slots.cm, "cm", budget, total, error);
    if (cm_rc != RV_OK) {
        return cm_rc;
    }

    // Compare against what the machine actually has.
    if (machine.ram_available < 0) {
        RV_LOG_ERR("pccheck", "machine RAM unknown, cannot show disc's {} byte(s) fit", total);
        return RV_ERR_INVAL;
    }

    if (total > machine.ram_available) {
        RV_LOG_ERR("pccheck", "disc needs {} byte(s) of RAM, this machine has {}", total, machine.ram_available);
        return RV_ERR_INVAL;
    }

    return RV_OK;
}

} // namespace rv_3dmppc
