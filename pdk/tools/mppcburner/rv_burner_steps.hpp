// The steps of mppcburner's commands, one list per command: [plan] and the [n/N] numbers are printed from it.
#pragma once

#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

#include "pdklib/rv_progress/rv_progress.hpp"

namespace rv_pdktools
{

// The labels of `mppcburner build` for a -t target; every target walks the same steps (compile may report "skipped").
inline std::vector<std::string> rv_burner_build_steps(std::string_view)
{
    return { "sources", "manifest", "compile", "assets", "burn" };
}

// The labels of `mppcburner setup web`.
inline std::vector<std::string> rv_burner_setup_steps()
{
    return { "check", "download", "verify", "unpack", "install", "embuilder", "mark" };
}

// The labels of `mppcburner bake-texture`.
inline std::vector<std::string> rv_burner_bake_texture_steps()
{
    return { "manifest", "texture" };
}

// The labels of `mppcburner inspect`; its report stays on stdout, the protocol is on stderr.
inline std::vector<std::string> rv_burner_inspect_steps()
{
    return { "inspect" };
}

// Prints `<tool>: [plan] ...` and then one `[n/N] label` line per label to `out`, numbers from the list.
inline void rv_burner_print_ladder(std::FILE *out, const std::string &tool, const std::vector<std::string> &labels)
{
    rv_pdklib::rv_progress_plan(tool, labels, out);
    for (std::size_t i = 0; i < labels.size(); ++i) {
        rv_pdklib::rv_progress_step(tool, static_cast<int>(i + 1), static_cast<int>(labels.size()), labels[i], "", out);
    }
}

} // namespace rv_pdktools
