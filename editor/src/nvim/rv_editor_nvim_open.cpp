// rv_editor_nvim::open implementation: navigate to file with buffer reuse.

#include "nvim/rv_editor_nvim.hpp"

namespace rv_editor
{

void rv_editor_nvim::open(int64_t win, const std::filesystem::path &path, int32_t line, int32_t col)
{
    if (!running() || win == 0) {
        return;
    }
    // If buffer for path is already loaded, reuse it without :edit.
    // Otherwise, :edit opens it; edit errors reported via rv_open_error notification.
    // Either way, set cursor. This avoids E37
    // (No write since last change) when navigating to an open modified buffer.
    exec_lua("local win, path, line, col = ...\n"
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
             "end",
        { std::to_string(win), path.string(), std::to_string(line), std::to_string(col) });
}

} // namespace rv_editor
