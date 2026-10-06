// rv_editor_nvim::open implementation: navigate to file with buffer reuse.

#include "nvim/rv_editor_nvim.hpp"

namespace rv_editor
{

namespace
{
// Notification method names from nvim
constexpr std::string_view notification_swap = "rv_swap";
constexpr std::string_view notification_open_error = "rv_open_error";

// Map keys in rv_swap notification
constexpr std::string_view swap_key_file = "file";
constexpr std::string_view swap_key_state = "state";
constexpr std::string_view swap_key_swap = "swap";
constexpr std::string_view swap_key_pid = "pid";

// Map keys in rv_open_error notification
constexpr std::string_view open_error_key_file = "file";
constexpr std::string_view open_error_key_msg = "msg";

// Swap state values from nvim
constexpr std::string_view swap_state_recoverable = "recoverable";
constexpr std::string_view swap_state_in_use = "in_use";
constexpr std::string_view swap_state_resolved = "resolved";

// Swap action choices for rv_swap_resolve in nvim
constexpr std::string_view swap_action_recover = "recover";
constexpr std::string_view swap_action_discard = "discard";

// Lua script: locate buffer by path (reuse if loaded, :edit otherwise) and set cursor at line/col
constexpr std::string_view lua_open_buffer_at_cursor =
    "local win, path, line, col = ...\n"
    "win = tonumber(win); line = tonumber(line); col = tonumber(col)\n"
    "local normalized_path = vim.fn.fnamemodify(path, ':p')\n"
    "local found_buf = nil\n"
    "for _, buf in ipairs(vim.api.nvim_list_bufs()) do\n"
    "  if vim.api.nvim_buf_is_loaded(buf) and vim.bo[buf].buftype == '' then\n"
    "    local buf_name = vim.api.nvim_buf_get_name(buf)\n"
    "    if vim.fn.fnamemodify(buf_name, ':p') == normalized_path then\n"
    "      found_buf = buf\n"
    "      break\n"
    "    end\n"
    "  end\n"
    "end\n"
    "vim.api.nvim_set_current_win(win)\n"
    "if found_buf then\n"
    "  vim.api.nvim_set_current_buf(found_buf)\n"
    "else\n"
    "  local ok, err = pcall(vim.cmd.edit, vim.fn.fnameescape(path))\n"
    "  if not ok then\n"
    "    vim.rpcnotify(0, \"rv_open_error\", { file = path, msg = tostring(err) })\n"
    "    return\n"
    "  end\n"
    "end\n"
    "if line > 0 then\n"
    "  pcall(vim.api.nvim_win_set_cursor, win, { line, col > 0 and col - 1 or 0 })\n"
    "end";

// Lua script: execute user's choice (recover or discard) for a swap file
constexpr std::string_view lua_swap_resolve_action = "local win, file, choice = ...\n"
                                                     "rv_swap_resolve(tonumber(win), file, choice)";

} // namespace

void rv_editor_nvim::open(int64_t win, const std::filesystem::path &path, int32_t line, int32_t col)
{
    if (!running() || win == 0) {
        return;
    }
    // If buffer for path is already loaded, reuse it without :edit.
    // Otherwise, :edit opens it; edit errors reported via rv_open_error notification.
    // Either way, set cursor. This avoids E37
    // (No write since last change) when navigating to an open modified buffer.
    exec_lua(std::string(lua_open_buffer_at_cursor),
        { std::to_string(win), path.string(), std::to_string(line), std::to_string(col) });
}

const rv_editor_nvim_swap *rv_editor_nvim::swap_for(const std::string &file) const
{
    const auto it = swaps_.find(file);
    return it != swaps_.end() ? &it->second : nullptr;
}

void rv_editor_nvim::swap_resolve(int64_t win, const std::string &file, bool recover)
{
    if (!running() || win == 0) {
        return;
    }
    const std::string choice = recover ? std::string(swap_action_recover) : std::string(swap_action_discard);
    exec_lua(std::string(lua_swap_resolve_action), { std::to_string(win), file, choice });
}

bool rv_editor_nvim::swap_notified(const std::string &method, const rv_editor_mpack &params, rv_editor_log &log)
{
    if (method == notification_swap && !params.items.empty()) {
        const rv_editor_mpack &m = params.items[0];
        const rv_editor_mpack *file = m.get(swap_key_file.data());
        const rv_editor_mpack *state = m.get(swap_key_state.data());
        if (file == nullptr || state == nullptr) {
            return true;
        }
        const std::string file_str = file->s;
        const std::string state_str = state->s;
        if (state_str == swap_state_recoverable) {
            const rv_editor_mpack *swap = m.get(swap_key_swap.data());
            if (swap == nullptr) {
                return true;
            }
            swaps_[file_str] = { swap->s, state_str, 0 };
            log.add(rv_editor_log_source::editor,
                rv_editor_log_level::warning,
                file_str + ": a crashed nvim left unsaved text in " + swap->s +
                    "; the file is read-only until Recover or Discard");
            return true;
        }
        if (state_str == swap_state_in_use) {
            const rv_editor_mpack *pid = m.get(swap_key_pid.data());
            int64_t pid_val = 0;
            if (pid != nullptr && pid->is(rv_editor_mpack::rv_editor_mpack_type::integer)) {
                pid_val = pid->i;
            }
            swaps_[file_str] = { "", state_str, pid_val };
            log.add(rv_editor_log_source::editor,
                rv_editor_log_level::warning,
                file_str + ": nvim process " + std::to_string(pid_val) + " is editing it; opened read-only");
            return true;
        }
        if (state_str == swap_state_resolved) {
            swaps_.erase(file_str);
            log.add(rv_editor_log_source::editor, rv_editor_log_level::info, file_str + ": swap file resolved");
            return true;
        }
        return true;
    }
    if (method == notification_open_error && !params.items.empty()) {
        const rv_editor_mpack &m = params.items[0];
        const rv_editor_mpack *file = m.get(open_error_key_file.data());
        const rv_editor_mpack *msg = m.get(open_error_key_msg.data());
        if (file == nullptr || msg == nullptr) {
            return true;
        }
        log.add(rv_editor_log_source::editor, rv_editor_log_level::error, file->s + ": cannot open: " + msg->s);
        return true;
    }
    return false;
}

} // namespace rv_editor
