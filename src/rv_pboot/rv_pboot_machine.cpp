#include "rv_pboot_machine.hpp"

#include "pdk/rv_err.h"
#include "pdklib/rv_logs/rv_logs.hpp"
#include "rv_pboot_args.hpp"
#include "rv_pconsole/rv_pcloader.hpp"
#include "rv_pconsole/rv_pcslots.hpp"
#include "rv_pconsole/mem/rv_pcvmem.hpp"

namespace rv_3dmppc
{

int64_t rv_pboot_mode_prepare(const rv_pboot_args &args, rv_pboot_mode_info &out)
{
    // This console has no fallback and no override flag for a kernel that
    // lacks MADV_POPULATE_WRITE: it is a hard requirement of the vmem the
    // memory stages below reserve.
    if (rv_pcvmem_probe_populate_write() < 0) {
        rv_console_print_error("this console requires a Linux kernel that implements MADV_POPULATE_WRITE");
        return RV_ERR_INVAL;
    }

    // An external disc will need its code extracted to a staging directory
    // before anything else about this run is built. Refuse now, before the
    // platform, the loader or the console exist, rather than discovering the
    // directory is unusable deep inside bring_up().
    if (args.disc_path != nullptr && rv_pcloader_probe_staging() < 0) {
        rv_console_print_error("no usable staging directory for the disc's code");
        return RV_ERR_INVAL;
    }

    // What this run's machine actually is: how much RAM the kernel says is
    // available. Whether a physical device actually came up never changes
    // the budget (rv_pboot_check_budget) - only what the platform itself
    // logs once it is brought up.
    out.ram_available = rv_pboot_mode_available_ram();

    return RV_OK;
}

void rv_pboot_mode_report(const rv_pboot_args &args, const rv_pcslots &slots, const rv_pboot_mode_info &machine)
{
    RV_LOG_INFO("main",
        "mode '{}' requested: platform={} ca={} cv={} cg={} cio={} cl={} cd={} cm={}",
        args.mode,
        rv_pcslots_name(slots.platform),
        rv_pcslots_name(slots.ca),
        rv_pcslots_name(slots.cv),
        rv_pcslots_name(slots.cg),
        rv_pcslots_name(slots.cio),
        rv_pcslots_name(slots.cl),
        rv_pcslots_name(slots.cd),
        rv_pcslots_name(slots.cm));
    RV_LOG_INFO("main", "ram available: {}", machine.ram_available);
}

} // namespace rv_3dmppc
