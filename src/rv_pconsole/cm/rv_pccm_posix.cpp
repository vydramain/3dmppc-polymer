#include "rv_pconsole/cm/rv_pccm_posix.hpp"

#include <cstring>
#include <format>

#include "pdk/rv_err.h"
#include "pdklib/rv_logs/rv_logs.hpp"

namespace rv_3dmppc
{

rv_pcbudget_cost rv_pccm_posix::evaluate(const rv_pdklib::rv_manifest_budget &budget)
{
    rv_pcbudget_cost cost{ rv_pccard::RV_PCCARD_HEADER_BYTES, {} };

    // card_ (rv_pccard's image_): header + one length entry per slot + the
    // slot payloads themselves.
    int64_t card_payload_bytes = 0;
    int64_t card_table_bytes = 0;
    if (rv_pcbudget_mul(cost, "budget.pccm.card_slots * card_slot_size", budget.pccm.card_slots,
            budget.pccm.card_slot_size, card_payload_bytes) ||
        rv_pcbudget_mul(cost, "budget.pccm.card_slots", budget.pccm.card_slots,
            rv_pccard::RV_PCCARD_LENGTH_ENTRY_BYTES, card_table_bytes)) {
        return cost;
    }

    if (rv_pcbudget_add(cost, "budget.pccm.card_slots", card_table_bytes) ||
        rv_pcbudget_add(cost, "budget.pccm.card_slots * card_slot_size", card_payload_bytes)) {
        return cost;
    }

    // rv_pccard refuses at construction to hold an image above its own
    // ceiling; refuse it here by name instead, before any disc code loads.
    if (cost.bytes > rv_pccard::RV_PCCARD_MAX_IMAGE_BYTES) {
        cost.reason = std::format(
            "'budget.pccm.card_slots' ({}) * 'budget.pccm.card_slot_size' ({}) needs a {} "
            "byte(s) card image, over the {} byte(s) this console's memory card can hold",
            budget.pccm.card_slots, budget.pccm.card_slot_size, cost.bytes,
            rv_pccard::RV_PCCARD_MAX_IMAGE_BYTES);
        return cost;
    }

    return cost;
}

rv_pccm_posix::rv_pccm_posix(const rv_pccm_conf &conf)
    : card_(conf.image_path, conf.card_slots, conf.card_slot_size)
{
}

bool rv_pccm_posix::slot_in_range(int64_t slot) const
{
    return slot >= 0 && slot < card_.slot_count();
}

// The geometry a disc validates its save blob against at disc_initialize. It is
// taken from the card rather than straight from the conf so a configuration the
// medium rejected reports 0 instead of a shape no slot actually has — and never
// a negative number, which a caller would read as an error.
int64_t rv_pccm_posix::card_slots()
{
    return card_.slot_count();
}

int64_t rv_pccm_posix::card_slot_size()
{
    return card_.slot_size();
}

int64_t rv_pccm_posix::card_size(int64_t slot)
{
    if (!slot_in_range(slot)) {
        return RV_ERR_INVAL;
    }
    // The contract enumerates only INVAL/NOENT here, but an unreadable medium
    // is neither: answering NOENT would state "no save yet" about a card that
    // may well hold one, and a disc would happily start a new game over it.
    // RV_ERR_IO is the honest answer, and callers test rc < 0 uniformly.
    if (!card_.medium_ok()) {
        return RV_ERR_IO;
    }
    const int64_t length = card_.slot_length(slot);
    if (length < 0) {
        return RV_ERR_NOENT;
    }
    return length;
}

int64_t rv_pccm_posix::card_read(int64_t slot, void *baddr, int64_t baddr_size)
{
    if (!slot_in_range(slot) || baddr == nullptr || baddr_size < 0) {
        return RV_ERR_INVAL;
    }
    if (!card_.medium_ok()) {
        return RV_ERR_IO;
    }
    const int64_t length = card_.slot_length(slot);
    // An empty slot is reported before the buffer is measured: "there is no
    // save" is a different fact from "your buffer is too small", and a disc
    // probing a fresh card must hear the first one.
    if (length < 0) {
        return RV_ERR_NOENT;
    }
    if (baddr_size < length) {
        return RV_ERR_INVAL;
    }
    if (length > 0) {
        std::memcpy(baddr, card_.slot_data(slot), static_cast<size_t>(length));
    }
    return length;
}

int64_t rv_pccm_posix::card_write(int64_t slot, const void *data, int64_t data_size)
{
    if (!slot_in_range(slot) || data == nullptr || data_size < 0 || data_size > card_.slot_size()) {
        return RV_ERR_INVAL;
    }
    // Atomicity lives one layer down, in rv_pccard::flush: when this returns
    // false the slot still holds its previous bytes, in RAM and on disk alike.
    if (!card_.slot_write(slot, data, data_size)) {
        return RV_ERR_IO;
    }
    return RV_OK;
}

int64_t rv_pccm_posix::card_erase(int64_t slot)
{
    if (!slot_in_range(slot)) {
        return RV_ERR_INVAL;
    }
    if (!card_.slot_erase(slot)) {
        return RV_ERR_IO;
    }
    return RV_OK;
}

} // namespace rv_3dmppc
