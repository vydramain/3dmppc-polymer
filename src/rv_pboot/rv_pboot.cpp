#include "rv_pboot.hpp"

#include <format>
#include <memory>
#include <optional>
#include <string>

#include "rv_dmain/rv_dmain.hpp"
#include "rv_pboot_args.hpp"
#include "rv_pboot_budget.hpp"
#include "rv_pboot_check.hpp"
#include "rv_pboot_conf.hpp"
#include "rv_pboot_mode.hpp"
#include "rv_pmem/rv_pcarena.hpp"
#include "pdklib/rv_logs/rv_logs.hpp"
#include "rv_pconsole/cd/rv_pczipmedium.hpp"
#include "rv_pconsole/rv_pcloader.hpp"
#include "rv_pconsole/rv_pconsole.hpp"
#include "rv_pconsole/rv_pconsole_conf.hpp"

// The built-in disc never goes through dlopen, so it has no entry points — but
// it needs the same rv_de table of hooks as any other. The macro expands the
// same thunks and hands back a function that wraps an ALREADY created object:
// the built-in disc's lifetime belongs to the stack frame below, not to a
// new/delete inside a disc.so.
RV_MPPC_DISC_TABLE_DEF(rv_service::rv_dmain, rv_dmain_table)

namespace rv_3dmppc
{

int rv_pboot_run(int argc, char **argv)
{
    // Parse the command line and validate the mode name. Nothing is
    // brought up here: a bad argument must cost a diagnostic, not a machine.
    rv_pboot_args args;

    int exit_code = 0;
    if (!rv_pboot_args_parse(argc, argv, args, exit_code)) {
        return exit_code;
    }

    // --selfcheck runs before anything else is brought up, and exits: it does
    // not boot a disc, does not touch SDL, does not need a mode.
    if (args.selfcheck) {
        return rv_pcarena_selfcheck() ? 0 : 1;
    }

    // The host owns SDL and is borrowed by the console, so it must outlive
    // both the loader and the console below, hence it is declared before
    // either. Brought up right after the mode name is validated, before the
    // disc's budget is even read.
    rv_pchost host;

    // Prepare the mode and learn the machine before any disc code, any
    // archive and any allocation.
    rv_pboot_mode_info machine;
    if (rv_pboot_mode_prepare(args, host, machine) < 0) {
        return 1;
    }

    // Report the preparation. This states what the mode is ready to offer;
    // it must not be read as any disc having been found compatible yet.
    rv_pboot_mode_report(args, host, machine);

    // The loader's teardown runs disc_shutdown(), a hook allowed to touch
    // every controller, so the loader must die before the console. Locals die
    // in reverse of declaration, hence the console is declared first, and
    // held in an optional because it cannot be constructed until the disc has
    // said what it needs.
    std::optional<rv_pconsole> console;
    rv_pcloader loader;

    // What the machine is going to be. A disc declares its requirements in
    // its manifest and those numbers are the machine it gets; the built-in
    // service test carries no manifest, so it runs on the reference
    // specification.
    const rv_pdklib::rv_manifest_budget *budget = nullptr;
    if (rv_pboot_budget_select(args, loader, budget) < 0) {
        return 1;
    }

    // Check the budget against the machine before any of the disc's code is
    // loaded. rv_pboot_check_budget() has already logged the specific reason;
    // this only names what is being refused.
    if (rv_pboot_check_budget(*budget, machine) < 0) {
        rv_console_print_error(std::format(
            "refusing to boot '{}'",
            args.disc_path != nullptr ? rv_pdklib::rv_log_escape(args.disc_path) : "built-in disc"));
        return 1;
    }

    // The disc's numbers become the machine's. Only the parameters that
    // belong to this run rather than to the disc come from the command line.
    rv_pconsole_conf conf;
    rv_pboot_conf_build(*budget, args, conf);

    host.configure(conf.cv.screen_width, conf.cv.screen_height, conf.cio.iport_count,
        conf.params.dump_frame_path);

    // Reserve and prepare memory. The resource check above only compared
    // MemAvailable against the declared budget, which is a forecast, not a
    // guarantee. The controllers below size several buffers (framebuffer,
    // ordering table, primitive buffer, memory-card image) as plain
    // std::vectors, so a budget that passed that check can still fail here,
    // and it fails by throwing rather than by returning an error. Catching
    // std::exception here turns that throw into the same ordinary refusal
    // the ready() check below produces for the arena's error path: a
    // diagnostic and exit code, not std::terminate/SIGABRT.
    try {
        console.emplace(conf, host, args.disc_path != nullptr ? &loader : nullptr);
    } catch (const std::exception &e) {
        // bad_alloc and length_error both derive from std::exception.
        RV_LOG_ERR("main", "failed to construct console: {}", e.what());
        rv_console_print_error(std::format("refusing to boot '{}'",
            args.disc_path != nullptr ? rv_pdklib::rv_log_escape(args.disc_path) : "built-in disc"));
        return 1;
    }

    // The reservation can still refuse what the resource check already
    // accepted: an address-space reservation is allowed to fail even when the
    // budget looked fine on paper. The controllers have already logged which
    // memory and how many bytes; this only names what is being refused.
    if (!console->ready()) {
        rv_console_print_error(std::format("refusing to boot '{}'",
            args.disc_path != nullptr ? rv_pdklib::rv_log_escape(args.disc_path) : "built-in disc"));
        return 1;
    }

    if (args.disc_path != nullptr) {
        if (loader.bring_up() < 0) {
            rv_console_print_error(std::format(
                "refusing to boot '{}'", rv_pdklib::rv_log_escape(args.disc_path)));
            return 1;
        }

        // The bytes the disc reads through rv_cd come out of the same file
        // its code came out of; that is what makes a `.mppcdisc` one object
        // rather than a program plus a loose pile of assets.
        console->drive().medium_insert(
            std::make_unique<rv_pczipmedium>(std::string(args.disc_path)));

        return static_cast<int>(console->disc_run(loader.disc()) < 0 ? 1 : 0);
    }

    // No disc in the machine: the built-in service test of the hardware. It
    // is how the console is checked without a game, and it is not a game
    // itself. `service` is a disc's namespace, not the machine's, and this
    // is the only line in the console that names a disc.
    rv_service::rv_dmain disc;
    rv_de de = rv_dmain_table(&disc);
    return static_cast<int>(console->disc_run(&de) < 0 ? 1 : 0);
}

} // namespace rv_3dmppc
