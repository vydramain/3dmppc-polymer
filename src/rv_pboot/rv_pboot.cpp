#include "rv_pboot.hpp"

#include <filesystem>
#include <format>
#include <memory>
#include <optional>
#include <string>
#include <system_error>

#include "rv_dmain/rv_dmain.hpp"
#include "rv_pboot_args.hpp"
#include "rv_pboot_args_cmd.hpp"
#include "rv_pboot_budget.hpp"
#include "rv_pboot_check.hpp"
#include "rv_pboot_conf.hpp"
#include "rv_pboot_discmedium.hpp"
#include "rv_pboot_machine.hpp"
#include "rv_pboot_modes.hpp"
#include "pdklib/rv_logs/rv_logs.hpp"
#include "rv_pconsole/platform/rv_pcsignals.hpp"
#include "rv_pconsole/rv_pcloader.hpp"
#include "rv_pconsole/rv_pconsole.hpp"
#include "rv_pconsole/rv_pconsole_conf.hpp"
#include "rv_pconsole/rv_pcslots.hpp"

// The built-in disc never goes through dlopen, so it has no entry points - but
// it needs the same rv_de table of hooks as any other. The macro expands the
// same thunks and hands back a function that wraps an ALREADY created object:
// the built-in disc's lifetime belongs to the stack frame below, not to a
// new/delete inside a disc.so.
RV_MPPC_DISC_TABLE_DEF(rv_service::rv_dmain, rv_dmain_table)

namespace rv_3dmppc
{

namespace
{

// Parses the command line, resolves the mode/slots and the pause/dump
// combination, and learns the machine - all of it before any disc code, any
// archive and any allocation. Returns true to continue booting; on false,
// `exit_code` is what rv_pboot_run must return immediately.
bool rv_pboot_preflight(int argc, char **argv, rv_pboot_args &args, rv_pcslots &slots,
    rv_pboot_mode_info &machine, int &exit_code)
{
    // Parse the command line and validate the mode name. Nothing is
    // brought up here: a bad argument must cost a diagnostic, not a machine.
    if (!rv_pboot_args_parse(argc, argv, args, exit_code)) {
        return false;
    }

    // SIGINT/SIGTERM become an ordinary shutdown request, seen through
    // rv_pcplatform::quit_requested(). Installed before any platform comes
    // up, so no platform library claims them.
    rv_pcsignals_install();

    // Resolve the preset and its per-slot overrides into the concrete choice
    // this run boots with. Nothing is brought up yet: a bad --mode or
    // --mode_<slot> must still cost a diagnostic, not a machine.
    if (!rv_pboot_modes_resolve(args, slots, exit_code)) {
        return false;
    }

    // A run whose cv slot is null never presents a frame, so a dump would
    // only ever be an empty frame. Refuse the combination here, before the
    // host or anything else is brought up, rather than write a useless file
    // - reached the same way whether cv=null came from the preset or from
    // --mode_cv.
    if (slots.cv == rv_pccv_impl::null && !args.dump_frame_path.empty()) {
        rv_console_print_error("cv is null, nothing to dump");
        exit_code = 2;
        return false;
    }

    // --paused stops the loop before frame 0, so something has to be able to
    // start it again. There are exactly two things that can: a command on the
    // development channel, and the Pause key, which needs a window to arrive
    // through. A run with neither would stop and stay stopped with no way out
    // but a signal - refused here rather than delivered as a hang, and checked
    // only now because it depends on the resolved platform and cv slot.
    const bool pause_can_be_lifted =
        args.dev ||
        (slots.platform == rv_pcplatform_impl::sdl3 && slots.cv != rv_pccv_impl::null);
    if (args.loop_paused && !pause_can_be_lifted) {
        rv_console_print_error(
            std::string("--paused would never be lifted: this mode has no window for the "
                        "pause key") +
            RV_PBOOT_ARGS_CMD_PAUSE_HINT);
        exit_code = 2;
        return false;
    }

    // Prepare the mode and learn the machine before any disc code, any
    // archive and any allocation.
    if (rv_pboot_mode_prepare(args, machine) < 0) {
        exit_code = 1;
        return false;
    }

    // Report the preparation. This states what the mode is ready to offer;
    // it must not be read as any disc having been found compatible yet.
    rv_pboot_mode_report(args, slots, machine);
    return true;
}

// Resolves cl, checks the budget against the machine, and builds the run's
// conf - all of it before any of the disc's code is loaded. Returns true to
// continue booting; on false, `exit_code` is what rv_pboot_run must return.
bool rv_pboot_prepare_conf(const rv_pboot_args &args, rv_pcslots &slots,
    const rv_pboot_mode_info &machine, const rv_pdklib::rv_manifest_budget *budget,
    bool medium_live, rv_pconsole_conf &conf, int &exit_code)
{
    // Resolve cl before evaluation so the row checked below is the row
    // rv_pccl_make later builds.
    slots.cl = rv_pccl_resolve(slots.cl, budget->pccl.script_memory_size);

    // Check the budget against the machine before any of the disc's code is
    // loaded. rv_pboot_check_budget() has already logged the specific reason;
    // this only names what is being refused.
    if (rv_pboot_check_budget(*budget, slots, machine) < 0) {
        rv_console_print_error(std::format(
            "refusing to boot '{}'",
            args.disc_path != nullptr ? rv_pdklib::rv_log_escape(args.disc_path) : "built-in disc"));
        exit_code = 1;
        return false;
    }

    // The disc's numbers become the machine's. Only the parameters that
    // belong to this run rather than to the disc come from the command line.
    rv_pboot_conf_build(*budget, args, slots, conf);

    // An archive cannot change while it is mounted; an unpacked directory can
    // - that is the whole reason a directory disc exists (see rv_pcloader.hpp
    // and mppcburner's --unpacked). medium_live is what lets the rest of the
    // console tell those two apart; it is rv_pboot_budget_select()'s own
    // verdict, from the one mount rv_pboot_disc_mount() already made, not a
    // second filesystem answer of this function's own.
    conf.params.medium_live = medium_live;
    return true;
}

} // namespace

std::filesystem::path rv_pboot_exe_dir()
{
    std::error_code ec;
    const std::filesystem::path exe = std::filesystem::read_symlink("/proc/self/exe", ec);
    return ec ? std::filesystem::path() : exe.parent_path();
}

int rv_pboot_run(int argc, char **argv)
{
    rv_pboot_args args;
    rv_pcslots slots;
    rv_pboot_mode_info machine;
    int exit_code = 0;
    if (!rv_pboot_preflight(argc, argv, args, slots, machine, exit_code)) {
        return exit_code;
    }

    // The loader's teardown runs disc_shutdown(), a hook allowed to touch
    // every controller, so the loader must die before the console. Locals die
    // in reverse of declaration, hence the console is declared first, and
    // held in an optional because it cannot be constructed until the disc has
    // said what it needs. The platform serves the console and is borrowed by
    // it, so it must outlive both and is declared before either; it is
    // brought up only after the budget check below, because nothing in the
    // budget depends on it.
    std::unique_ptr<rv_pcplatform> platform;
    std::optional<rv_pconsole> console;
    rv_pcloader loader;

    // What the machine is going to be. A disc declares its requirements in
    // its manifest and those numbers are the machine it gets; the built-in
    // service test carries no manifest, so it runs on the reference
    // specification.
    const rv_pdklib::rv_manifest_budget *budget = nullptr;
    bool medium_live = false;
    if (rv_pboot_budget_select(args, loader, budget, medium_live) < 0) {
        return 1;
    }

    rv_pconsole_conf conf;
    if (!rv_pboot_prepare_conf(args, slots, machine, budget, medium_live, conf, exit_code)) {
        return exit_code;
    }

    // Open exactly the endpoints the virtual devices will use.
    rv_pcplatform_wants wants;
    wants.window = slots.cv != rv_pccv_impl::null;
    wants.gamepads = slots.cio != rv_pccio_impl::null;
    wants.audio = slots.ca != rv_pcca_impl::null;
    platform = rv_pcplatform_make(slots.platform, wants);

    // Reserve and prepare memory. The resource check above only compared
    // MemAvailable against the declared budget, which is a forecast, not a
    // guarantee. The controllers below size several buffers (framebuffer,
    // ordering table, primitive buffer, memory-card image) as plain
    // std::vectors, so a budget that passed that check can still fail here,
    // and it fails by throwing rather than by returning an error. Catching
    // std::exception here turns that throw into the same ordinary refusal
    // the ready() check below produces for the vmem's error path: a
    // diagnostic and exit code, not std::terminate/SIGABRT.
    try {
        console.emplace(conf, *platform, args.disc_path != nullptr ? &loader : nullptr);
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

        // The bytes the disc reads through rv_cd come out of the same place its
        // code came out of; that is what makes a disc ONE object rather than a
        // program plus a loose pile of assets - and it holds for both media.
        // The archive is the shipped form and cannot change while mounted; the
        // unpacked directory is the development form, and its entries may be
        // symlinks to the author's own files, which is exactly what makes a
        // live script reload possible (conf.params.medium_live above). Which
        // medium that is, per this build, is rv_pboot_disc_medium()'s call
        // (rv_pboot_discmedium.hpp) - a player build's medium_live is always
        // false, so it always gets the archive medium back.
        console->drive().medium_insert(rv_pboot_disc_medium(args.disc_path, conf.params.medium_live));

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
