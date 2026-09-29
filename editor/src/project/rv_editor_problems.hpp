#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

namespace rv_editor
{

// One diagnostic that names a place: from the latest build, or from a language server.
struct rv_editor_problem
{
    std::filesystem::path file; // absolute; resolved against the project root when relative
    int32_t line = 0;
    int32_t column = 0;         // 0 when the message gives none
    bool error = true;          // else a warning
    std::string message;
    std::string source = "build"; // "build", or the language server's name
};

// Reads one build line: the compiler's "<file>:<line>[:<col>]: error|warning: <text>"
// and mppcburner's "cannot compile '<file>': <chunk>:<line>: <text>". False for any
// other line: an unknown format is never given a place it does not state.
bool rv_editor_problem_parse(std::string_view text, const std::filesystem::path &root, rv_editor_problem &out);

} // namespace rv_editor
