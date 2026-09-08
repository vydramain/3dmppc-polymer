#include "rv_burner_process.hpp"

#include <sys/wait.h>

#include <array>
#include <cstddef>
#include <cstdio>
#include <string>

namespace rv_pdktools
{

// One read from the child's pipe. Nothing depends on the value: the loop below
// keeps reading until the pipe closes, so this only trades syscalls for memory.
static constexpr std::size_t k_capture_chunk_bytes = 4096;

// Exit status the shell reports for a process killed by a signal: 128 plus the
// signal number. WEXITSTATUS is meaningless in that case, so this is what a
// caller comparing against 0 gets instead.
static constexpr int k_status_killed_by_signal = 128;

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
