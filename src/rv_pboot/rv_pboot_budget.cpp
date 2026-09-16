#include "rv_pboot_budget.hpp"

#include <filesystem>
#include <format>
#include <system_error>

#include "pdk/rv_err.h"
#include "pdklib/rv_logs/rv_logs.hpp"
#include "rv_pconsole/rv_pcloader.hpp"

namespace rv_3dmppc
{

int64_t rv_pboot_budget_select(const rv_pboot_args &args, rv_pcloader &loader,
    const rv_pdklib::rv_manifest_budget *&out)
{
    if (args.disc_path != nullptr) {
        // A directory disc (mppcburner's --unpacked output) and a .mppcdisc
        // archive go through different rv_pcloader stages from here on, but
        // arrive at the same checked-out manifest; which one the positional
        // path names is nothing more exotic than the filesystem's own answer.
        std::error_code ec;
        const bool is_dir = std::filesystem::is_directory(std::filesystem::path(args.disc_path), ec);
        const int64_t mount_rc = is_dir ? loader.mount_dir(args.disc_path) : loader.mount(args.disc_path);
        if (mount_rc < 0) {
            rv_console_print_error(std::format(
                "refusing to boot '{}'", rv_pdklib::rv_log_escape(args.disc_path)));
            return RV_ERR_INVAL;
        }
        out = &loader.info().budget;
    } else {
        out = &rv_pboot_budget_builtin();
    }
    return RV_OK;
}

} // namespace rv_3dmppc
