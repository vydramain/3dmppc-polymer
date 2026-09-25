// Saving and switching projects in the code editor's nvim, each with nvim's own
// answer: a request is not a write.

#include "nvim/rv_editor_nvim.hpp"

namespace rv_editor
{

namespace
{

using mtype = rv_editor_mpack::rv_editor_mpack_type;

// Writes the modified buffers named in the first argument ("3,7"; empty: all)
// and returns one { id, name, ok, error } per buffer.
constexpr const char *rv_editor_lua_save = R"lua(
local ids = ...
local want = {}
for s in string.gmatch(ids, '%d+') do want[tonumber(s)] = true end
-- nvim's own message, one line: "E505: ... is read-only", not the Lua traceback.
local function reason(err)
    local s = tostring(err)
    return s:match('([EW]%d+:[^\n]*)') or s:match('^[^\n]*')
end
-- A write that raised no error but left the buffer modified was refused another
-- way (a question nobody could answer): say the best reason nvim left behind.
local function write(b, name)
    -- 'readonly' nvim set while the file could not be written stays after the
    -- permission came back; the user asked for this write, and the system allows it.
    if vim.bo[b].readonly and vim.fn.filewritable(name) == 1 then vim.bo[b].readonly = false end
    vim.v.errmsg = ''
    local ok, res = pcall(vim.api.nvim_buf_call, b, function()
        return vim.api.nvim_exec2('write', { output = true })
    end)
    if not ok then return false, reason(res) end
    if not vim.bo[b].modified then return true, '' end
    local why = vim.v.errmsg ~= '' and vim.v.errmsg or (type(res) == 'table' and res.output or '')
    if why == '' and vim.fn.filereadable(name) == 1 and vim.fn.filewritable(name) == 0 then
        why = 'no permission to write the file'
    end
    if why == '' then why = 'nvim did not write it: a question it asked went unanswered' end
    return false, reason(why)
end
local out = {}
for _, b in ipairs(vim.api.nvim_list_bufs()) do
    if vim.api.nvim_buf_is_loaded(b) and vim.bo[b].buftype == '' and vim.bo[b].modified
        and (ids == '' or want[b]) then
        local name = vim.api.nvim_buf_get_name(b)
        local ok, err = false, 'no file name: Save As gives it one'
        if name ~= '' then
            ok, err = write(b, name)
        end
        out[#out + 1] = { id = b, name = name, ok = ok, error = ok and '' or reason(err) }
    end
end
return out
)lua";

// Buffer, then path: `:saveas`, which refuses a file that exists.
constexpr const char *rv_editor_lua_save_as = R"lua(
local b, path = tonumber((select(1, ...))), select(2, ...)
local ok, err = pcall(vim.api.nvim_buf_call, b, function() vim.cmd('saveas ' .. vim.fn.fnameescape(path)) end)
if ok and vim.bo[b].modified then ok, err = false, 'still modified after the write' end
local s = tostring(err)
local reason = ok and '' or (s:match('([EW]%d+:[^\n]*)') or s:match('^[^\n]*'))
return { { id = b, name = vim.api.nvim_buf_get_name(b), ok = ok, error = reason } }
)lua";

// The new root: nothing may be modified; then every window shows a fresh empty
// buffer, the buffers no window shows go, and nvim moves there.
constexpr const char *rv_editor_lua_switch_root = R"lua(
local root = ...
for _, b in ipairs(vim.api.nvim_list_bufs()) do
    if vim.api.nvim_buf_is_loaded(b) and vim.bo[b].modified then
        return 'a buffer still has unsaved changes'
    end
end
for _, w in ipairs(vim.api.nvim_list_wins()) do
    vim.api.nvim_win_call(w, function() vim.cmd('enew') end)
end
for _, b in ipairs(vim.api.nvim_list_bufs()) do
    if vim.fn.bufwinid(b) == -1 then pcall(vim.api.nvim_buf_delete, b, {}) end
end
vim.fn.chdir(root)
return ''
)lua";

// nvim's error reply is [type, message].
std::string rv_editor_reply_error(const rv_editor_mpack &error)
{
    if (error.is(mtype::array) && error.items.size() >= 2) {
        return error.items[1].s;
    }
    return error.s.empty() ? "nvim refused the request" : error.s;
}

std::vector<rv_editor_nvim_saved> rv_editor_saved_list(const rv_editor_mpack &result)
{
    std::vector<rv_editor_nvim_saved> out;
    for (const rv_editor_mpack &m : result.items) {
        rv_editor_nvim_saved s;
        if (const rv_editor_mpack *v = m.get("id")) {
            s.id = v->i;
        }
        if (const rv_editor_mpack *v = m.get("name")) {
            s.name = v->s;
        }
        if (const rv_editor_mpack *v = m.get("ok")) {
            s.ok = v->b;
        }
        if (const rv_editor_mpack *v = m.get("error")) {
            s.error = v->s;
        }
        out.push_back(std::move(s));
    }
    return out;
}

rv_editor_nvim_rpc::rv_editor_nvim_reply rv_editor_save_reply(rv_editor_nvim_save_done done)
{
    return [done = std::move(done)](const rv_editor_mpack &error, const rv_editor_mpack &result) {
        if (!error.is(mtype::nil)) {
            done({}, rv_editor_reply_error(error));
            return;
        }
        done(rv_editor_saved_list(result), {});
    };
}

} // namespace

void rv_editor_nvim::save(const std::vector<int64_t> &ids, rv_editor_nvim_save_done done)
{
    if (!running()) {
        done({}, "the code editor is not running");
        return;
    }
    std::string list;
    for (const int64_t id : ids) {
        list += (list.empty() ? "" : ",") + std::to_string(id);
    }
    exec_lua(rv_editor_lua_save, { list }, rv_editor_save_reply(std::move(done)));
}

void rv_editor_nvim::save_as(int64_t id, const std::filesystem::path &path, rv_editor_nvim_save_done done)
{
    if (!running()) {
        done({}, "the code editor is not running");
        return;
    }
    exec_lua(rv_editor_lua_save_as, { std::to_string(id), path.string() }, rv_editor_save_reply(std::move(done)));
}

void rv_editor_nvim::switch_root(const std::filesystem::path &root, std::function<void(const std::string &)> done)
{
    if (!running()) {
        done({});
        return;
    }
    exec_lua(rv_editor_lua_switch_root, { root.string() },
        [done = std::move(done)](const rv_editor_mpack &error, const rv_editor_mpack &result) {
            done(error.is(mtype::nil) ? result.s : rv_editor_reply_error(error));
        });
}

} // namespace rv_editor
