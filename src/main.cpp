// 3dmppc entry point
//
// The console is game-agnostic; all game logic lives behind on the Disc.
// See docs/README.md for the console/disc relations.
//
// The command line is documented in one place, rv_console_print_usage() in
// rv_infra/rv_console_print.hpp, so the help text cannot drift away from a
// comment nobody prints.
#include <getopt.h>

#include <charconv>
#include <cstdlib>
#include <format>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "rv_dmain/rv_dmain.hpp"
#include "rv_infra/rv_console_print.hpp"
#include "rv_infra/rv_pcarena.hpp"
#include "rv_infra/rv_pcmachine.hpp"
#include "pdklib/rv_logs/rv_logs.hpp"
#include "rv_pconsole/cd/rv_pczipmedium.hpp"
#include "rv_pconsole/compatibility/check/check_launch_disc.hpp"
#include "rv_pconsole/rv_pcbudget_builtin.hpp"
#include "rv_pconsole/rv_pcloader.hpp"
#include "rv_pconsole/rv_pconsole.hpp"
#include "rv_pconsole/rv_pconsole_conf.hpp"

namespace {

// Everything getopt_long can produce, and nothing else: no SDL, no
// rv_pconsole_conf, no allocation beyond the strings themselves. Building the
// real conf is a separate step, after the whole command line has been read.
struct rv_console_args {
    bool headless = false;
    bool fixed_step = false;
    bool mute = false;
    bool no_audio = false;
    bool selfcheck = false;
    uint64_t scale = 3;
    uint64_t max_frames = 0;
    std::string medium_path;
    std::string memcard_path;
    std::string dump_frame_path;
    std::string mode = "sdl3";
    const char* disc_path = nullptr;
};

// The only backend this console knows how to boot. A name outside this list
// is a bad ARGUMENT (exit 2, usage printed), not a machine failure — same
// family of mistake as an unrecognized flag.
constexpr const char* kKnownModes[] = {"sdl3"};

bool is_known_mode(const std::string& mode) {
    for (const char* known : kKnownModes) {
        if (mode == known) return true;
    }
    return false;
}

// Strict non-negative decimal. Anything else — empty, a sign, letters, trailing
// junk, a value too large for the type — is a bad ARGUMENT, not a zero.
bool parse_u64(const char* text, uint64_t& out) {
    if (text == nullptr || text[0] == '\0' || text[0] == '+' || text[0] == '-') return false;
    const char* end = text + std::string_view(text).size();
    auto [ptr, ec] = std::from_chars(text, end, out);
    return ec == std::errc() && ptr == end;
}

}  // namespace

int main(int argc, char** argv) {
    rv_console_args args;

    // Note what is NOT here: any game's name. The console mounts whatever
    // medium it is pointed at and boots the disc it is handed on the command
    // line; with nothing at all it runs the built-in skeleton against an empty
    // drive.
    static struct option long_opts[] = {{"headless", no_argument, 0, 'H'},
                                        {"fixed-step", no_argument, 0, 'F'},
                                        {"scale", required_argument, 0, 's'},
                                        {"frames", required_argument, 0, 'n'},
                                        {"disc", required_argument, 0, 'd'},
                                        {"memcard", required_argument, 0, 'm'},
                                        {"mute", no_argument, 0, 'M'},
                                        {"dump-frame", required_argument, 0, 'D'},
                                        {"mode", required_argument, 0, 'o'},
                                        {"no-audio", no_argument, 0, 'A'},
                                        {"selfcheck", no_argument, 0, 'Y'},
                                        {0, 0, 0, 0}};

    // A. parse argv
    int c;
    while ((c = getopt_long(argc, argv, "HFMs:n:d:m:D:", long_opts, NULL)) != -1) {
        switch (c) {
            case 'H':
                args.headless = true;
                break;
            case 'F':
                args.fixed_step = true;
                break;
            case 'M':
                args.mute = true;
                break;
            case 'A':
                args.no_audio = true;
                break;
            case 's':
                if (!parse_u64(optarg, args.scale) || args.scale == 0) {
                    rv_3dmppc::rv_console_print_error(
                        std::format("bad value for --scale: '{}'", rv_pdklib::rv_log_escape(optarg)));
                    rv_3dmppc::rv_console_print_usage(stderr);
                    return 2;
                }
                break;
            case 'n':
                if (!parse_u64(optarg, args.max_frames)) {
                    rv_3dmppc::rv_console_print_error(
                        std::format("bad value for --frames: '{}'", rv_pdklib::rv_log_escape(optarg)));
                    rv_3dmppc::rv_console_print_usage(stderr);
                    return 2;
                }
                break;
            case 'd':
                args.medium_path = optarg;
                break;
            case 'm':
                args.memcard_path = optarg;
                break;
            case 'D':
                args.dump_frame_path = optarg;
                break;
            case 'o':
                args.mode = optarg;
                break;
            case 'Y':
                args.selfcheck = true;
                break;
            case '?':
                // getopt has already named the offending option on stderr.
                rv_3dmppc::rv_console_print_usage(stderr);
                return 2;
        }
    }

    // --headless means no window AND no rasterization (rv_pconsole::disc_run
    // skips frame_render() entirely), so a frame dump would only ever be an
    // empty frame — refuse the combination here rather than write a useless
    // file.
    if (args.headless && !args.dump_frame_path.empty()) {
        rv_3dmppc::rv_console_print_error("--headless and --dump-frame cannot be combined");
        rv_3dmppc::rv_console_print_usage(stderr);
        return 2;
    }

    // --selfcheck runs before anything else is brought up, and exits: it does
    // not boot a disc, does not touch SDL, does not need a mode.
    if (args.selfcheck) {
        return rv_3dmppc::rv_pcarena_selfcheck() ? 0 : 1;
    }

    // B. validate the mode name
    if (!is_known_mode(args.mode)) {
        rv_3dmppc::rv_console_print_error(
            std::format("unknown --mode '{}', available: sdl3", rv_pdklib::rv_log_escape(args.mode.c_str())));
        rv_3dmppc::rv_console_print_usage(stderr);
        return 2;
    }

    // getopt_long has left optind on the first thing that was not a flag. One
    // positional argument is expected — the disc — and more than one is a typo
    // worth refusing rather than silently ignoring.
    args.disc_path = (optind < argc) ? argv[optind] : nullptr;
    if (optind + 1 < argc) {
        rv_3dmppc::rv_console_print_error(
            std::format("expected at most one disc path, got {}", argc - optind));
        rv_3dmppc::rv_console_print_usage(stderr);
        return 2;
    }

    // C. prepare the mode, learn the machine — before any disc code, any
    // archive and any allocation. This console has no fallback and no
    // override flag for a kernel that lacks MADV_POPULATE_WRITE: it is a hard
    // requirement of the arena the memory stages below reserve.
    if (rv_3dmppc::rv_pcarena_probe_populate_write() < 0) {
        rv_3dmppc::rv_console_print_error(
            "this console requires a Linux kernel that implements MADV_POPULATE_WRITE");
        return 1;
    }

    // Stage C, continued: an external disc will need its code extracted to a
    // staging directory before anything else about this run is built. Refuse
    // now, before the host, the loader or the console exist, rather than
    // discovering the directory is unusable deep inside bring_up().
    if (args.disc_path != nullptr && rv_3dmppc::rv_pcloader_probe_staging() < 0) {
        rv_3dmppc::rv_console_print_error("no usable staging directory for the disc's code");
        return 1;
    }

    // DESTRUCTION ORDER IS LOAD-BEARING: the host owns SDL and is borrowed by
    // the console, so it must outlive both the loader and the console below —
    // hence it is declared before either. Brought up right after the mode
    // name is validated, before the disc's budget is even read.
    rv_3dmppc::rv_pchost host;
    host.prepare(!args.headless, true, !args.no_audio);

    // What this run's machine actually is: which subsystems came up (or were
    // never asked for) and how much RAM the kernel says is available.
    rv_3dmppc::rv_pcmachine_info machine;
    machine.ram_available = rv_3dmppc::rv_pcmachine_available_ram();
    machine.video_enabled = !args.headless && host.video_ready();
    machine.audio_enabled = !args.no_audio && host.audio_ready();

    // D. report the preparation. This states what the MODE is ready to offer —
    // it must not be read as any disc having been found compatible yet.
    RV_LOG_INFO("main", "mode '{}' prepared", args.mode);
    if (args.headless) {
        RV_LOG_INFO("main", "video: off (--headless)");
    } else if (!host.video_ready()) {
        RV_LOG_INFO("main", "video: off (subsystem refused to come up)");
    } else {
        RV_LOG_INFO("main", "video: on");
    }
    if (!host.gamepad_ready()) {
        RV_LOG_INFO("main", "gamepad: off (subsystem refused to come up)");
    } else {
        RV_LOG_INFO("main", "gamepad: on");
    }
    if (args.no_audio) {
        RV_LOG_INFO("main", "audio: off (--no-audio)");
    } else if (!host.audio_ready()) {
        RV_LOG_INFO("main", "audio: off (subsystem refused to come up)");
    } else {
        RV_LOG_INFO("main", "audio: on");
    }
    RV_LOG_INFO("main", "ram available: {}", machine.ram_available);

    // DESTRUCTION ORDER IS LOAD-BEARING. The loader's teardown runs
    // disc_shutdown(), a hook allowed to touch every controller, so the loader
    // must die BEFORE the console. Locals die in reverse of declaration, hence
    // the console is declared FIRST — and held in an optional, because it cannot
    // be CONSTRUCTED until the disc has said what it needs.
    std::optional<rv_3dmppc::rv_pconsole> console;
    rv_3dmppc::rv_pcloader loader;

    // E1/E2. What the machine is going to be. A disc declares its requirements
    // in its manifest and those numbers ARE the machine it gets; the built-in
    // service test carries no manifest, so it runs on the reference
    // specification.
    const rv_pdklib::rv_manifest_budget *budget = nullptr;
    if (args.disc_path != nullptr) {
        if (loader.mount(args.disc_path) < 0) {
            rv_3dmppc::rv_console_print_error(std::format(
                "refusing to boot '{}'", rv_pdklib::rv_log_escape(args.disc_path)));
            return 1;
        }
        budget = &loader.info().budget;
    } else {
        budget = &rv_3dmppc::rv_pcbudget_builtin();
    }

    // E3. Check the budget against the machine, before any of the disc's code
    // is loaded. check_launch_disc() has already logged the specific reason;
    // this only names what is being refused.
    if (rv_3dmppc::rv_compatibility_check_launch_disc(*budget, machine) < 0) {
        rv_3dmppc::rv_console_print_error(std::format(
            "refusing to boot '{}'",
            args.disc_path != nullptr ? rv_pdklib::rv_log_escape(args.disc_path) : "built-in disc"));
        return 1;
    }

    // F. The disc's numbers become the machine's. Only the parameters that belong
    // to THIS RUN rather than to the disc come from the command line.
    rv_3dmppc::rv_pconsole_conf conf;
    conf.ca.voice_count = budget->pcca.voice_count;
    conf.ca.sound_memory_size = budget->pcca.sound_memory_size;
    conf.cv.screen_width = budget->pccv.screen_width;
    conf.cv.screen_height = budget->pccv.screen_height;
    conf.cv.texture_max_width = budget->pccv.texture_max_width;
    conf.cv.texture_max_height = budget->pccv.texture_max_height;
    conf.cv.video_memory_size = budget->pccv.video_memory_size;
    conf.cv.frame_capacity = budget->pccv.frame_capacity;
    conf.cv.ot_bucket_count = budget->pccv.ot_bucket_count;
    conf.cio.iport_count = budget->pccio.iport_count;
    conf.cm.card_slots = budget->pccm.card_slots;
    conf.cm.card_slot_size = budget->pccm.card_slot_size;

    conf.params.headless = args.headless;
    conf.params.fixed_step = args.fixed_step;
    conf.params.scale = args.scale;
    conf.params.max_frames = args.max_frames;
    conf.params.dump_frame_path = args.dump_frame_path;
    conf.ca.mute = args.mute;
    conf.ca.no_audio = args.no_audio;
    conf.cd.medium_path = args.medium_path;
    conf.cm.image_path = args.memcard_path;

    host.configure(conf.cv.screen_width, conf.cv.screen_height, conf.cio.iport_count,
                   conf.params.dump_frame_path);

    // G/H. reserve and prepare memory
    console.emplace(conf, host, args.disc_path != nullptr ? &loader : nullptr);

    // H1. The reservation can still refuse what E3 already accepted (an
    // address-space reservation is allowed to fail even when the budget looked
    // fine on paper). The controllers have already logged which memory and how
    // many bytes; this only names what is being refused.
    if (!console->ready()) {
        rv_3dmppc::rv_console_print_error(std::format("refusing to boot '{}'",
            args.disc_path != nullptr ? rv_pdklib::rv_log_escape(args.disc_path) : "built-in disc"));
        return 1;
    }

    // I. load and run
    if (args.disc_path != nullptr) {
        // The code, only now: everything above decided WHAT machine it gets.
        if (loader.bring_up() < 0) {
            rv_3dmppc::rv_console_print_error(std::format(
                "refusing to boot '{}'", rv_pdklib::rv_log_escape(args.disc_path)));
            return 1;
        }

        // One archive, two roles. The bytes the disc reads through rv_cd come out
        // of the SAME file its code came out of — that is what makes a
        // `.mppcdisc` one object rather than a program plus a loose pile of
        // assets. The drive never learns it is now talking to a zip (PATTERN:
        // strategy, rv_pcmedium.hpp); only this line knows.
        console->drive().medium_insert(
            std::make_unique<rv_3dmppc::rv_pczipmedium>(std::string(args.disc_path)));

        return static_cast<int>(console->disc_run(*loader.disc()) < 0 ? 1 : 0);
    }

    // No disc in the machine: the built-in service test of the hardware. It is
    // linked into the console on purpose — it is how the console is checked
    // without a game, and it is not a game itself. `service` is a DISC's
    // namespace, not the machine's, and this is the only line in the console
    // that names a disc: the service test is embedded deliberately.
    rv_service::rv_dmain disc;
    return static_cast<int>(console->disc_run(disc) < 0 ? 1 : 0);
}
