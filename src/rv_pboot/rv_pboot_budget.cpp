#include "rv_pboot_budget.hpp"

#include <format>

#include "pdk/rv_err.hpp"
#include "pdklib/rv_logs/rv_logs.hpp"
#include "rv_pconsole/rv_pcloader.hpp"

namespace rv_3dmppc
{

int64_t rv_pboot_budget_select(const rv_pboot_args &args, rv_pcloader &loader,
    const rv_pdklib::rv_manifest_budget *&out)
{
    if (args.disc_path != nullptr) {
        if (loader.mount(args.disc_path) < 0) {
            rv_console_print_error(std::format(
                "refusing to boot '{}'", rv_pdklib::rv_log_escape(args.disc_path)));
            return rv_pdk::RV_ERR_INVAL;
        }
        out = &loader.info().budget;
    } else {
        out = &rv_pboot_budget_builtin();
    }
    return rv_pdk::RV_OK;
}

} // namespace rv_3dmppc
