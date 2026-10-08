// File operations on buffers: rename, delete, unsaved check.

#include "nvim/rv_editor_nvim.hpp"

#include <filesystem>

namespace rv_editor
{

namespace
{

std::string rv_editor_normalize_path(const std::filesystem::path &p)
{
    return std::filesystem::absolute(p).lexically_normal().string();
}

bool rv_editor_path_in_dir(const std::string &norm_path, const std::string &norm_dir)
{
    if (norm_path.size() <= norm_dir.size()) {
        return false;
    }
    if (norm_path.compare(0, norm_dir.size(), norm_dir) != 0) {
        return false;
    }
    return norm_path[norm_dir.size()] == std::filesystem::path::preferred_separator;
}

// Rename buffer from old path to new: read file fresh, delete old buffer.
constexpr const char *rv_editor_lua_rename_buffer = R"lua(
local old_path, new_path = ...
for _, b in ipairs(vim.api.nvim_list_bufs()) do
    if vim.api.nvim_buf_is_loaded(b) and vim.bo[b].buftype == '' then
        local name = vim.api.nvim_buf_get_name(b)
        if name == old_path then
            for _, w in ipairs(vim.api.nvim_list_wins()) do
                if vim.api.nvim_win_get_buf(w) == b then
                    vim.api.nvim_win_call(w, function()
                        vim.cmd('edit ' .. vim.fn.fnameescape(new_path))
                    end)
                end
            end
            pcall(vim.api.nvim_buf_delete, b, {})
            break
        end
    end
end
return ''
)lua";

// Unload buffer: show empty buffer in its windows, then delete it.
constexpr const char *rv_editor_lua_delete_buffer = R"lua(
local path = ...
for _, b in ipairs(vim.api.nvim_list_bufs()) do
    if vim.api.nvim_buf_is_loaded(b) and vim.bo[b].buftype == '' then
        local name = vim.api.nvim_buf_get_name(b)
        if name == path then
            for _, w in ipairs(vim.api.nvim_list_wins()) do
                if vim.api.nvim_win_get_buf(w) == b then
                    vim.api.nvim_win_call(w, function() vim.cmd('enew') end)
                end
            end
            pcall(vim.api.nvim_buf_delete, b, {})
            break
        end
    end
end
return ''
)lua";

} // namespace

std::string rv_editor_nvim::buffers_unsaved_at(const std::filesystem::path &path) const
{
    const std::string norm_path = rv_editor_normalize_path(path);

    for (const rv_editor_nvim_buffer &b : buffers_) {
        if (!b.modified || b.name.empty()) {
            continue;
        }
        const std::string norm_buf = rv_editor_normalize_path(b.name);
        if (norm_buf == norm_path || rv_editor_path_in_dir(norm_buf, norm_path)) {
            return b.name + " has unsaved changes: save or discard them first";
        }
    }
    return "";
}

void rv_editor_nvim::rename_buffers_at(const std::filesystem::path &from, const std::filesystem::path &to)
{
    if (!running()) {
        return;
    }
    const std::string norm_from = rv_editor_normalize_path(from);
    const std::string norm_to = rv_editor_normalize_path(to);

    for (const rv_editor_nvim_buffer &b : buffers_) {
        if (b.name.empty()) {
            continue;
        }
        const std::string norm_buf = rv_editor_normalize_path(b.name);
        if (norm_buf == norm_from) {
            exec_lua(rv_editor_lua_rename_buffer, { norm_buf, norm_to });
            break;
        }
        if (rv_editor_path_in_dir(norm_buf, norm_from)) {
            const std::string relative = norm_buf.substr(norm_from.size() + 1);
            const std::filesystem::path new_path = to / relative;
            exec_lua(rv_editor_lua_rename_buffer, { norm_buf, rv_editor_normalize_path(new_path) });
        }
    }
}

void rv_editor_nvim::delete_buffers_at(const std::filesystem::path &path)
{
    if (!running()) {
        return;
    }
    const std::string norm_path = rv_editor_normalize_path(path);

    for (const rv_editor_nvim_buffer &b : buffers_) {
        if (b.name.empty()) {
            continue;
        }
        const std::string norm_buf = rv_editor_normalize_path(b.name);
        if (norm_buf == norm_path || rv_editor_path_in_dir(norm_buf, norm_path)) {
            exec_lua(rv_editor_lua_delete_buffer, { norm_buf });
        }
    }
}

} // namespace rv_editor
