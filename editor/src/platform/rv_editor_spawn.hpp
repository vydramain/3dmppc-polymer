#pragma once

#include <csignal>
#include <filesystem>
#include <string>
#include <sys/types.h>
#include <utility>
#include <vector>

namespace rv_editor
{

// Which process group the child leads.
enum class rv_editor_spawn_group {
    inherit, // the editor's own group
    own,     // setpgid(0, 0): a group of its own
    session, // setsid(): a new session, whose first terminal becomes its controlling one
};

// Child setup, applied between fork and execve in the order of the fields.
struct rv_editor_spawn_setup {
    rv_editor_spawn_group group = rv_editor_spawn_group::inherit;
    std::string tty;                            // opened O_RDWR as stdin, when not empty
    std::vector<std::pair<int, int>> dup_pairs; // (from, to); from == to clears close-on-exec
    std::filesystem::path cwd;                  // chdir target, when not empty
    sigset_t signals_default{};                 // each signal in the set goes back to SIG_DFL
    bool set_mask = false;                      // true: the child's mask is `mask`; false: inherited
    sigset_t mask{};                            // both sets: fill with sigemptyset/sigaddset/sigfillset
};

// Starts `path` with argv and envp, both nullptr-terminated and prepared by the caller, without a
// shell. RV_OK with the child's pid in `pid`: exec happened, or the child died before exec without
// a report (its status shows at the caller's reap). Otherwise the errno maps the same for every
// caller: ENOENT -> RV_ERR_NOENT, EINVAL -> RV_ERR_INVAL, else RV_ERR_IO; the reason is in `error`.
// When the failed child cannot be killed or reaped, the code is RV_ERR_IO whatever the errno.
// PR_SET_PDEATHSIG is Linux only: elsewhere the window until the guard registers the child stays.
// An exec of a setuid/setgid `path` resets PDEATHSIG. Registers nothing with the process guard.
int rv_editor_spawn(const char *path,
    char *const argv[],
    char *const envp[],
    const rv_editor_spawn_setup &setup,
    pid_t &pid,
    std::string &error);

} // namespace rv_editor
