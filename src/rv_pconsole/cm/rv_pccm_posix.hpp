// The rv_cm implementation of the reference console: geometry out of
// rv_pccm_conf, bytes out of rv_pccard. Everything this class adds over the
// image is contract semantics — argument validation and the exact rv_err
// vocabulary — so the medium never has to know what a rv_err is.
#pragma once

#include "rv_pconsole/cm/rv_pccard.hpp"
#include "rv_pconsole/cm/rv_pccm.hpp"
#include "rv_pconsole/rv_pcbudget.hpp"
#include "rv_pconsole/rv_pconsole_conf.hpp"

namespace rv_3dmppc
{
class rv_pccm_posix final : public rv_pccm
{
private:
    // The image is the single source of truth for the geometry too: it is what
    // conf asked for, minus anything the medium refused.
    rv_pccard card_;

    // True when `slot` names a slot this console has.
    bool slot_in_range(int64_t slot) const;

public:
    explicit rv_pccm_posix(const rv_pccm_conf &conf);
    ~rv_pccm_posix() = default;

    // The peak host bytes card_ (rv_pccard's image_) allocates for `budget`:
    // header + one length entry per slot + the slot payloads themselves,
    // or a refusal when that image would be over
    // rv_pccard::RV_PCCARD_MAX_IMAGE_BYTES, the same refusal rv_pccard would
    // otherwise only log after construction.
    static rv_pcbudget_cost evaluate(const rv_pdklib::rv_manifest_budget &budget);

    int64_t card_slots() override;

    int64_t card_slot_size() override;

    int64_t card_size(int64_t slot) override;

    int64_t card_read(int64_t slot, void *baddr, int64_t baddr_size) override;

    int64_t card_write(int64_t slot, const void *data, int64_t data_size) override;

    int64_t card_erase(int64_t slot) override;

    // Does the medium this controller owns actually exist? False when the
    // card failed to come up with the geometry it was asked for.
    bool valid() const override
    {
        return card_.valid();
    }
};

} // namespace rv_3dmppc
