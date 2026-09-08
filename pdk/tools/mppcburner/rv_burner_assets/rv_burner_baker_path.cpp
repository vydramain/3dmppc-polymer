#include "rv_burner_baker_path.hpp"

#include <unistd.h>

#include <array>
#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <string_view>
#include <system_error>

namespace fs = std::filesystem;

namespace rv_pdktools
{

// The file name both tools are built under. Never spelled inline below, so the
// three search roots cannot drift apart.
static constexpr const char *k_baker_name = "mppcbaker";

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

bool rv_pdktools::find_baker(const std::string &hint, std::string &out, std::string &error)
{
    // --- an explicit --baker wins ---
    //
    // And it wins even when it is wrong: a hint that is not executable is an
    // error rather than a reason to keep searching, because silently running a
    // different mppcbaker than the one asked for is worse than not running one.
    if (!hint.empty()) {
        if (!is_executable(hint)) {
            error = "--baker '" + hint + "' is not an executable file";
            return false;
        }
        out = hint;
        return true;
    }

    // --- next to ourselves ---
    //
    // Three shapes of build tree: both binaries side by side, and the two ways
    // cmake nests a target's output under its own subdirectory.
    const fs::path self = executable_directory();
    if (!self.empty()) {
        const fs::path candidates[] = {
            self / k_baker_name,
            self / k_baker_name / k_baker_name,
            self.parent_path() / k_baker_name / k_baker_name
        };
        for (const fs::path &candidate : candidates) {
            if (is_executable(candidate)) {
                out = candidate.string();
                return true;
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
                const fs::path candidate = fs::path(std::string(head)) / k_baker_name;
                if (is_executable(candidate)) {
                    out = candidate.string();
                    return true;
                }
            }
            if (colon == std::string_view::npos) {
                break;
            }
            rest.remove_prefix(colon + 1);
        }
    }

    error = "mppcbaker not found next to mppcburner or in $PATH; pass --baker PATH";
    return false;
}
