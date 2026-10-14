#include "rv_burner_process.hpp"

#include <sys/wait.h>
#include <unistd.h>

#include <array>
#include <cstddef>
#include <cstdio>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;

namespace rv_pdktools
{

// One read from the child's pipe. Nothing depends on the value: the loop below
// keeps reading until the pipe closes, so this only trades syscalls for memory.
static constexpr std::size_t k_capture_chunk_bytes = 4096;

// Exit status the shell reports for a process killed by a signal: 128 plus the
// signal number. WEXITSTATUS is meaningless in that case, so this is what a
// caller comparing against 0 gets instead.
static constexpr int k_status_killed_by_signal = 128;

// Upper bound for the path read out of /proc/self/exe. Linux caps a path at
// PATH_MAX (4096 on every configuration this tool runs on) and readlink does not
// terminate what it writes, so one byte is left for the terminator below.
static constexpr std::size_t k_path_buffer_bytes = 4096;

} // namespace rv_pdktools

std::string rv_pdktools::shell_quote(const std::string &text)
{
    std::string out = "'";
    for (const char c : text) {
        if (c == '\'') {
            out += "'\\''";
        } else {
            out += c;
        }
    }
    out += "'";
    return out;
}

int rv_pdktools::run_capture(const std::string &command, std::string &output)
{
    output.clear();

    // --- start the child ---
    //
    // `2>&1` merges the child's stderr into the pipe popen gives us, so one
    // capture holds everything it said, in the order it said it.
    const std::string full = command + " 2>&1";
    std::FILE *pipe = ::popen(full.c_str(), "r");
    if (pipe == nullptr) {
        return -1;
    }

    // --- drain the pipe ---
    //
    // fread returns short only at end of pipe or on error, and both mean the
    // child has nothing more to say, so a zero-length read ends the loop.
    std::array<char, k_capture_chunk_bytes> buffer{};
    while (true) {
        const std::size_t got = std::fread(buffer.data(), 1, buffer.size(), pipe);
        if (got == 0) {
            break;
        }
        output.append(buffer.data(), got);
    }

    // --- collect the status ---

    const int status = ::pclose(pipe);
    if (status == -1) {
        return -1;
    }

    // WIFEXITED is true only when the child returned from main or called exit;
    // only then does WEXITSTATUS hold a code it chose itself.
    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }

    return k_status_killed_by_signal;
}

void rv_pdktools::dump_child_output(const std::string &output)
{
    if (output.empty()) {
        return;
    }

    std::fwrite(output.data(), 1, output.size(), stderr);
    if (output.back() != '\n') {
        std::fputc('\n', stderr);
    }
}

// The directory holding the RUNNING binary — not the working directory, which
// the user chose, and not argv[0], which the caller can set to anything.
//
// /proc/self/exe is a Linux symlink pointing at the executable file of the
// calling process, so reading it answers "where am I installed?" exactly. An
// empty path means the answer is unavailable and the caller must fall back.
std::filesystem::path rv_pdktools::executable_directory()
{
    std::array<char, k_path_buffer_bytes> buffer{};
    const ssize_t got = ::readlink("/proc/self/exe", buffer.data(), buffer.size() - 1);
    if (got <= 0) {
        return fs::path();
    }

    buffer[static_cast<std::size_t>(got)] = '\0';
    return fs::path(buffer.data()).parent_path();
}
