// What the console says to the person who started it.
//
// The shape is getopt's, because getopt writes into the same stream and cannot
// be told otherwise:
//
//   3dmppc: unrecognized option '--frame'    <- printed by getopt
//   3dmppc: expected at most one disc path   <- printed by us
//
// No level, no tag, no source position: the reader is starting a console, not
// debugging one. Everything the console says about its own work goes through
// RV_LOG_* from pdklib/rv_stdio.hpp instead.
#pragma once

#include <cstdio>
#include <string>

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
	std::fprintf(stderr, "%s: %s\n", rv_console_progname(), message.c_str());
}

inline void rv_console_print_usage(std::FILE *stream)
{
	std::fprintf(stream,
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
		"  -H, --headless       Run without a window. Pair with --frames.\n"
		"  -n, --frames N       Stop after N frames. 0 runs until quit.\n"
		"  -F, --fixed-step     Use a fixed 1/60 dt, for reproducible runs.\n"
		"  -d, --disc PATH      Medium to mount in the drive: a DIRECTORY of\n"
		"                       loose assets, the development shortcut that\n"
		"                       needs no packaging step. A packaged .mppcdisc\n"
		"                       goes in the positional argument instead and\n"
		"                       brings its own medium. Empty means no disc.\n"
		"  -m, --memcard PATH   Memory-card image. Default: memcard.mppccard.\n"
		"  -M, --mute           Silence the audio output stage.\n"
		"  -D, --dump-frame P   Write the last presented frame to P as a binary\n"
		"                       PPM.\n");
}

} // namespace rv_3dmppc
