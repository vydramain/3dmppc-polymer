#pragma once

#include <string>
#include <sys/types.h>
#include <vector>

namespace rv_editor
{

// A session's processes, found by walking /proc (Linux). Both return RV_OK or
// RV_ERR_IO with the reason in `error`.
int rv_editor_session_members(pid_t sid, std::vector<pid_t> &out, std::string &error);
int rv_editor_signal_session(pid_t sid, int sig, std::string &error);

// Appends a reason to `error`: when two steps fail, both are kept.
void rv_editor_add_reason(std::string &error, const std::string &reason);

} // namespace rv_editor
