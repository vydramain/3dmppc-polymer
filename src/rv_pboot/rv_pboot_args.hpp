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

inline void rv_console_print_usage(std::FILE *stream)
{
    rv_pdklib::rv_fprintf(stream,
        "3dmppc - the console: mount a disc and run it\n"
        "\n"
        "Usage:\n"
        "  3dmppc [options] [<disc.mppcdisc>]\n"
        "\n"
        "The positional argument is the disc to boot: the console mounts that\n"
        "archive, loads the code inside it and runs it. With no disc it falls\n"
        "back to the built-in rv_dmain, a service test of the hardware rather\n"
        "than a game.\n"
        "\n"
        "Options:\n"
        "  -s, --scale N        Window magnification over native 320x240.\n"
        "                       Default: 3.\n"
        "  -H, --headless       Run without a window and without rasterizing any\n"
        "                       frame. Pair with --frames. Cannot be combined\n"
        "                       with --dump-frame.\n"
        "  -n, --frames N       Stop after N frames. 0 runs until quit.\n"
        "  -F, --fixed-step     Use a fixed 1/60 dt, for reproducible runs.\n"
        "  -d, --disc PATH      Medium to mount in the drive: a DIRECTORY of\n"
        "                       loose assets, the development shortcut that\n"
        "                       needs no packaging step. A packaged .mppcdisc\n"
        "                       goes in the positional argument instead and\n"
        "                       brings its own medium. Empty means no disc.\n"
        "  -m, --memcard PATH   Memory-card image. Default: memcard.mppccard.\n"
        "  -M, --mute           Silence the audio output stage.\n"
        "      --no-audio       Do not open the audio device at all. Stronger\n"
        "                       than --mute: no device, voices report idle.\n"
        "  -D, --dump-frame P   Write the last presented frame to P as a binary\n"
        "                       PPM. Cannot be combined with --headless.\n"
        "      --mode=NAME      Console backend to run. Default: sdl3.\n"
        "                       Available: sdl3.\n"
        "      --selfcheck      Run internal self-checks and exit.\n");
}

// Everything getopt_long can produce, and nothing else: no SDL, no
// rv_pconsole_conf, no allocation beyond the strings themselves. Building the
// real conf is a separate step, after the whole command line has been read.
struct rv_pboot_args {
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
    const char *disc_path = nullptr;
};

// Parse argv into `out`. Returns true on success. Returns false when the
// caller must return `exit_code` immediately (2 for a bad command line)
// without doing anything else: no disc, no SDL, nothing.
bool rv_pboot_args_parse(int argc, char **argv, rv_pboot_args &out, int &exit_code);

} // namespace rv_3dmppc
