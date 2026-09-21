// Usage and argument diagnostics: what the console says to the person who
// started it.
//
// The shape is getopt's, because getopt writes into the same stream and cannot
// be told otherwise:
//
//   3dmppc: unrecognized option '--frame'    <- printed by getopt
//   3dmppc: expected at most one disc path   <- printed by us
//
// No level, no tag, no source position: the reader is starting a console, not
// debugging one. Everything the console says about its own work goes through
// RV_LOG_* from pdklib/rv_logs/rv_logs.hpp instead.
#pragma once

#include <cstdint>
#include <cstdio>
#include <string>

#include "pdklib/rv_stdio/rv_stdio.hpp"

#ifdef __GLIBC__
#include <cerrno> // IWYU pragma: keep - declares program_invocation_short_name
#endif

namespace rv_3dmppc
{

// Taken from argv[0] rather than written down, so the prefix keeps matching
// getopt's after the binary is renamed.
inline const char *rv_console_progname()
{
#ifdef __GLIBC__
    return program_invocation_short_name;
#else
    return "3dmppc";
#endif
}

inline void rv_console_print_error(const std::string &message)
{
    rv_pdklib::rv_fprintf(stderr, "%s: %s\n", rv_console_progname(), message.c_str());
}

// Defined in rv_pboot_args.cpp: the IMPL lists and the built-in preset
// sentence are built from the registry tables (rv_pcslots.hpp,
// rv_pboot_modes.cpp), not written out here.
void rv_console_print_usage(std::FILE *stream);

// Everything getopt_long can produce, and nothing else: no SDL, no
// rv_pconsole_conf, no allocation beyond the strings themselves. Building the
// real conf is a separate step, after the whole command line has been read.
struct rv_pboot_args {
    bool fixed_step = false;
    bool mute = false;

#if RV_DEVTOOLS
    // `dev` opens the command channel on stdin (see
    // rv_pconsole/platform/rv_pcdevchan.hpp). Not even a field outside a dev
    // build: --dev is not an option a player binary's getopt table carries
    // (rv_pboot_args.cpp), so there is nothing here for it to set.
    bool dev = false;
#endif

    // Start with the frame loop STOPPED, before frame 0, so the first
    // controllable moment comes before the disc has drawn anything.
    //
    // Named for the loop and not for the development runtime, because the pause
    // is not a development feature: the Pause key drives the same stop in an
    // ordinary run. What --paused needs is only a way to be LIFTED - a command
    // channel or a window that can receive the key - and boot refuses the
    // combination that has neither.
    bool loop_paused = false;
    uint64_t scale = 3;
    uint64_t max_frames = 0;
    std::string medium_path;
    std::string memcard_path;
    std::string dump_frame_path;
    std::string mode = "default";

    // Per-slot overrides of the preset named by `mode`. Empty = not given.
    std::string mode_platform;
    std::string mode_ca;
    std::string mode_cv;
    std::string mode_cio;
    std::string mode_cl;
    std::string mode_cd;
    std::string mode_cm;

    const char *disc_path = nullptr;
};

// Parse argv into `out`. Returns true on success. Returns false when the
// caller must return `exit_code` immediately (2 for a bad command line)
// without doing anything else: no disc, no SDL, nothing.
bool rv_pboot_args_parse(int argc, char **argv, rv_pboot_args &out, int &exit_code);

// Whether --dev was given, answerable in code that runs in EVERY build
// without itself reaching into a field a player build never declares. A
// player build always answers false: it never parses --dev, so there is no
// other honest answer.
inline bool rv_pboot_args_dev(const rv_pboot_args &args)
{
#if RV_DEVTOOLS
    return args.dev;
#else
    (void)args;
    return false;
#endif
}

} // namespace rv_3dmppc
