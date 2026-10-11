#include "rv_burner_baker_path.hpp"

#include <unistd.h>

#include <array>
#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <string_view>
#include <system_error>

#include "pdk/rv_err.h"

namespace fs = std::filesystem;

namespace rv_pdktools
{

// Upper bound for the path read out of /proc/self/exe. Linux caps a path at
// PATH_MAX (4096 on every configuration this tool runs on) and readlink does not
// terminate what it writes, so one byte is left for the terminator below.
static constexpr std::size_t k_path_buffer_bytes = 4096;

// Is this a file the kernel would agree to execute? Both halves matter: a
// directory can carry the execute bit too, and it means something else there.
static bool is_executable(const fs::path &path)
{
    std::error_code ec;
    return fs::is_regular_file(path, ec) && ::access(path.c_str(), X_OK) == 0;
}

// The directory holding the RUNNING binary — not the working directory, which
// the user chose, and not argv[0], which the caller can set to anything.
//
// /proc/self/exe is a Linux symlink pointing at the executable file of the
// calling process, so reading it answers "where am I installed?" exactly. An
// empty path means the answer is unavailable and the caller must fall back.
static fs::path executable_directory()
{
    std::array<char, k_path_buffer_bytes> buffer{};
    const ssize_t got = ::readlink("/proc/self/exe", buffer.data(), buffer.size() - 1);
    if (got <= 0) {
        return fs::path();
    }

    buffer[static_cast<std::size_t>(got)] = '\0';
    return fs::path(buffer.data()).parent_path();
}

} // namespace rv_pdktools

int rv_pdktools::find_baker(const std::string &hint, const std::string &program, std::string &out, std::string &error)
{
    // --- an explicit --baker wins ---
    //
    // A directory is searched as given; a file stands for the directory it lives in.
    // And it wins even when it is wrong: a hint that does not name a directory or a
    // file is an error rather than a reason to keep searching, because silently
    // running a different program than the one asked for is worse than not running one.
    if (!hint.empty()) {
        std::error_code ec;
        const fs::file_status given_status = fs::status(hint, ec);
        if (ec) {
            error = "--baker '" + hint + "' cannot be read: " + ec.message();
            return RV_ERR_INVAL;
        }
        if (!fs::is_directory(given_status) && !fs::is_regular_file(given_status)) {
            error = "--baker '" + hint + "' is neither a directory nor a regular file";
            return RV_ERR_INVAL;
        }

        // A file hint is resolved first, so a symlink searches the directory of its target.
        fs::path dir = hint;
        if (fs::is_regular_file(given_status)) {
            dir = fs::canonical(hint, ec).parent_path();
            if (ec) {
                error = "--baker '" + hint + "' cannot be resolved: " + ec.message();
                return RV_ERR_INVAL;
            }
        }

        const fs::path candidate = dir / program;
        if (!is_executable(candidate)) {
            // A file that exists but cannot run is a bad hint (INVAL); a missing one is not found (NOENT).
            const bool exists = fs::is_regular_file(candidate, ec);
            error = exists ? "'" + program + "' in '" + dir.string() + "' is not executable (--baker)" :
                             "'" + program + "' not found in '" + dir.string() + "' (--baker)";
            return exists ? RV_ERR_INVAL : RV_ERR_NOENT;
        }
        out = candidate.string();
        return RV_OK;
    }

    // --- next to ourselves ---
    //
    // Three shapes of build tree: both programs side by side, and the two ways
    // cmake nests a target's output under its own subdirectory.
    const fs::path self = executable_directory();
    if (!self.empty()) {
        const fs::path candidates[] = { self / program, self / program / program, self.parent_path() / program / program };
        for (const fs::path &candidate : candidates) {
            if (is_executable(candidate)) {
                out = candidate.string();
                return RV_OK;
            }
        }
    }

    // --- $PATH ---
    //
    // Walked by hand rather than handed to a shell: the entries are directory
    // names, not shell words, and an entry containing a space or a quote must
    // not be re-interpreted on the way.
    const char *path_env = std::getenv("PATH");
    if (path_env != nullptr) {
        std::string_view rest(path_env);
        while (!rest.empty()) {
            const std::size_t colon = rest.find(':');
            const std::string_view head = rest.substr(0, colon);
            if (!head.empty()) {
                const fs::path candidate = fs::path(std::string(head)) / program;
                if (is_executable(candidate)) {
                    out = candidate.string();
                    return RV_OK;
                }
            }
            if (colon == std::string_view::npos) {
                break;
            }
            rest.remove_prefix(colon + 1);
        }
    }

    error = "'" + program + "' not found next to mppcburner or in $PATH; pass --baker DIR";
    return RV_ERR_NOENT;
}
