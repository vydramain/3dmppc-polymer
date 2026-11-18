#pragma once

#include <cstdint>
#include <string>

#include "log/rv_editor_log.hpp"

namespace rv_editor
{

enum class rv_editor_build_alert_kind {
    none,
    not_started, // "Build did not start": the editor did not start the build
    failed,      // "Build failed": the burner refused or failed
};

// What the ask area at the top of Problems says; none while there is nothing to say.
struct rv_editor_build_alert {
    rv_editor_build_alert_kind kind = rv_editor_build_alert_kind::none;
    std::string reason;
};

// The reason a build job failed: the text of the last error-level line the burner printed in this job (build or
// candidate lines with seq >= first_seq), else `exit_text`.
std::string rv_editor_build_alert_reason(const rv_editor_log &log, uint64_t first_seq, const std::string &exit_text);

} // namespace rv_editor
