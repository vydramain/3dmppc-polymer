// Diagnostic output for everything built in this repository: the console, a
// disc, and the authoring tools.
//
// One line looks like this, and the two bracketed fields are fixed width so a
// stream of them reads as columns:
//
//   [tool][DBG] zipwrite: entry deflated 1200 -> 430 (rv_zipwrite.cpp:212)
//   [mppc][ERR] pcloader: refusing to boot 'hello'
//   [disc][INF] gameloop: level 2 reached
//
// Header-only on purpose: pdklib is an INTERFACE target and compiles no
// translation unit of its own.
#pragma once

#include <cstddef>
#include <cstdio>
#include <format>
#include <string>

#include "pdklib/rv_stdio.hpp"

// Who is speaking, four characters wide. Set once per build target, e.g.
// target_compile_definitions(3dmppc PRIVATE RV_LOG_ORIGIN="mppc"), and never by
// calling code: an origin nobody has to remember to pass is an origin that is
// always right. A target that forgets it prints "????" in every line.
#ifndef RV_LOG_ORIGIN
#define RV_LOG_ORIGIN "????"
#endif

// Gives every loaded module its own copy of the threshold. A disc is a dlopen'd
// .so, and an inline variable with default visibility would be merged with the
// console's copy by the dynamic linker, so a disc raising its own verbosity
// would raise the console's too.
#if defined(__GNUC__) || defined(__clang__)
#define RV_STDIO_MODULE_LOCAL __attribute__((visibility("hidden")))
#else
#define RV_STDIO_MODULE_LOCAL
#endif

namespace rv_pdklib
{

// Ordered by urgency; a level prints when it is at most the threshold.
enum rv_log_level : int {
	RV_LOG_LEVEL_EMERG = 0,
	RV_LOG_LEVEL_ERR = 1,
	RV_LOG_LEVEL_WARN = 2,
	RV_LOG_LEVEL_INFO = 3,
	RV_LOG_LEVEL_DBG = 4,
};

// Loudest level that still prints. One copy per module.
RV_STDIO_MODULE_LOCAL inline rv_log_level rv_log_threshold = RV_LOG_LEVEL_INFO;

inline void rv_log_set_threshold(rv_log_level level)
{
	rv_log_threshold = level;
}

inline bool rv_log_enabled(rv_log_level level)
{
	return level <= rv_log_threshold;
}

// Three characters, so the level column never shifts.
inline const char *rv_log_level_name(rv_log_level level)
{
	switch (level) {
	case RV_LOG_LEVEL_EMERG:
		return "EMG";
	case RV_LOG_LEVEL_ERR:
		return "ERR";
	case RV_LOG_LEVEL_WARN:
		return "WRN";
	case RV_LOG_LEVEL_INFO:
		return "INF";
	case RV_LOG_LEVEL_DBG:
		return "DBG";
	}
	return "???";
}

// Make a string from OUTSIDE the console safe to put in a log line.
//
// Anything a disc hands the console - a resource name, a disc title - is
// untrusted text, and a log is where untrusted text does damage: a newline in
// it forges a whole line, an ANSI escape repaints the reader's terminal. Every
// byte outside printable ASCII becomes \xNN, and the result is cut to
// `max_length` so a runaway string cannot push the real message off screen.
//
// RV_LOG_WARN("cd", "rejected '{}'", resname) is the bug; wrapping resname in
// this is the fix.
inline std::string rv_log_escape(const char *text, std::size_t max_length = 64)
{
	if (nullptr == text) {
		return "(null)";
	}

	std::string out;
	std::size_t taken = 0;
	for (const char *p = text; *p != '\0'; ++p) {
		if (taken >= max_length) {
			out += "...";
			break;
		}

		const unsigned char c = static_cast<unsigned char>(*p);
		// A quote is escaped too: callers wrap the result in quotes, and an
		// unescaped one would fake the end of the field.
		if (c >= 0x20 && c < 0x7F && c != '\'' && c != '\\') {
			out += static_cast<char>(c);
		} else {
			out += std::format("\\x{:02X}", c);
		}
		++taken;
	}
	return out;
}

// Called through RV_LOG_*, which has already tested the level. The source
// position rides along only on DBG: on the other levels it is noise for
// whoever is reading the output rather than debugging the program.
inline void rv_log_emit(rv_log_level level, const char *origin, const char *tag,
	const char *file, int line, const std::string &message)
{
	if (RV_LOG_LEVEL_DBG == level) {
		rv_fprintf(stderr, "[%s][%s] %s: %s (%s:%d)\n", origin,
			rv_log_level_name(level), tag, message.c_str(), file, line);
		return;
	}
	rv_fprintf(stderr, "[%s][%s] %s: %s\n", origin, rv_log_level_name(level), tag,
		message.c_str());
}

} // namespace rv_pdklib

// The macros live outside the namespace because that is where the preprocessor
// puts them regardless: a #define between namespace braces is not a member of
// anything. Every name they expand to is spelled from the global root, since
// the expansion is looked up at the call site, and a call site may be main().
//
// The level is tested BEFORE std::format runs. Formatting a message the
// threshold will drop costs an allocation for nothing, which is exactly what a
// silenced RV_LOG_DBG must not do.
#define RV_LOG(level, tag, ...)                                                                    \
	do {                                                                                       \
		if (::rv_pdklib::rv_log_enabled(level)) {                                          \
			::rv_pdklib::rv_log_emit((level), RV_LOG_ORIGIN, (tag), __FILE__, __LINE__, \
				std::format(__VA_ARGS__));                                         \
		}                                                                                  \
	} while (0)

#define RV_LOG_EMERG(tag, ...) RV_LOG(::rv_pdklib::RV_LOG_LEVEL_EMERG, (tag), __VA_ARGS__)
#define RV_LOG_ERR(tag, ...) RV_LOG(::rv_pdklib::RV_LOG_LEVEL_ERR, (tag), __VA_ARGS__)
#define RV_LOG_WARN(tag, ...) RV_LOG(::rv_pdklib::RV_LOG_LEVEL_WARN, (tag), __VA_ARGS__)
#define RV_LOG_INFO(tag, ...) RV_LOG(::rv_pdklib::RV_LOG_LEVEL_INFO, (tag), __VA_ARGS__)
#define RV_LOG_DBG(tag, ...) RV_LOG(::rv_pdklib::RV_LOG_LEVEL_DBG, (tag), __VA_ARGS__)
