#pragma once

#include <format>  // IWYU pragma: keep — used by RV_LOG below; tools miss macro bodies
#include <string>

namespace rv_3dmppc {

enum rv_log_level : int {
    RV_LOG_LEVEL_EMERG = 0,
    RV_LOG_LEVEL_ERR = 1,
    RV_LOG_LEVEL_WARN = 2,
    RV_LOG_LEVEL_INFO = 3,
    RV_LOG_LEVEL_DBG = 4,
};

void rv_log_emit(rv_log_level lvl, const char* tag, const char* file, int line, std::string msg);

// Make a string from OUTSIDE the console safe to put in a log line.
//
// Anything a disc hands the console — a resource name, a disc title — is
// untrusted text, and a log is a place where untrusted text does damage: a
// newline inside it forges a whole log line, and an ANSI escape repaints or
// clears the reader's terminal. So every byte outside printable ASCII is
// rendered as \xNN, and the result is truncated to `max_length` (a runaway
// string must not push the real message off the screen).
//
// This is why the console must never format such a string directly into a
// message: `RV_LOG_WARN("cd", "rejected '{}'", resname)` is the bug, and
// `rv_log_escape(resname)` is the fix.
std::string rv_log_escape(const char* text, std::size_t max_length = 64);

}  // namespace rv_3dmppc

// The macros sit OUTSIDE the namespace on purpose, because that is where they
// have always really been: the preprocessor runs before any of C++ exists, so a
// #define written between namespace braces is not a member of anything — the
// name lands in every translation unit that includes this header either way.
// Writing them inside would only suggest a containment that is not there.
//
// What the macro expands INTO is ordinary code, looked up at the CALL site, so
// every name below is spelled in full from the global root. Unqualified, they
// would compile only from within rv_3dmppc — and an entry point like main() is
// by definition outside it. The enumerator is the name that actually forces
// this: rv_log_emit alone would be reachable through argument-dependent lookup
// (its first argument is an rv_3dmppc type), but RV_LOG_LEVEL_ERR is not a
// function call, so nothing widens the search for it.
#define RV_LOG(lvl, tag, ...)                                                                 \
    do {                                                                                      \
        ::rv_3dmppc::rv_log_emit((lvl), (tag), __FILE__, __LINE__, std::format(__VA_ARGS__)); \
    } while (0)

#define RV_LOG_ERR(tag, ...) RV_LOG(::rv_3dmppc::RV_LOG_LEVEL_ERR, (tag), __VA_ARGS__)
#define RV_LOG_WARN(tag, ...) RV_LOG(::rv_3dmppc::RV_LOG_LEVEL_WARN, (tag), __VA_ARGS__)
#define RV_LOG_INFO(tag, ...) RV_LOG(::rv_3dmppc::RV_LOG_LEVEL_INFO, (tag), __VA_ARGS__)
#define RV_LOG_DBG(tag, ...) RV_LOG(::rv_3dmppc::RV_LOG_LEVEL_DBG, (tag), __VA_ARGS__)
