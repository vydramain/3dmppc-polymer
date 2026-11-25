// Saving and switching projects in the code editor's nvim, each with nvim's own
// answer: a request is not a write.

#include <algorithm>

#include "nvim/rv_editor_nvim.hpp"
#include "text/rv_editor_text.hpp"

namespace rv_editor
{

namespace
{

using mtype = rv_editor_mpack::rv_editor_mpack_type;

constexpr auto rv_editor_state_reply_timeout = std::chrono::seconds(2);

// nvim error reply format: [type, message] (msgpack-rpc spec)
constexpr size_t error_message_index = 1;
constexpr size_t error_array_min_size = 2;

// Buffer save result field names from Lua script return value
constexpr std::string_view buf_result_key_id = "id";
constexpr std::string_view buf_result_key_name = "name";
constexpr std::string_view buf_result_key_ok = "ok";
constexpr std::string_view buf_result_key_error = "error";

// Separator for comma-delimited buffer ID list passed to Lua script
constexpr const char *buf_id_separator = ",";

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
    -- 'readonly' can outlast an unwritable path. A write can also recreate a
    -- missing file when its directory is writable.
    if vim.bo[b].readonly then
        local can_create = vim.fn.getftype(name) == '' and vim.fn.filewritable(vim.fn.fnamemodify(name, ':h')) == 2
        if vim.fn.filewritable(name) == 1 or can_create then vim.bo[b].readonly = false end
    end
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

// The current head marks the next redo block on the active branch. It has a
// preceding undo block unless it is the first entry in that branch.
constexpr const char *rv_editor_lua_history = R"lua(
local b = tonumber(...)
if not vim.api.nvim_buf_is_loaded(b) or not vim.bo[b].modifiable then return { false, false } end
return vim.api.nvim_buf_call(b, function()
    local entries = vim.fn.undotree().entries
    for i, entry in ipairs(entries) do
        if entry.curhead then return { i > 1, true } end
    end
    return { #entries > 0, false }
end)
)lua";

constexpr const char *rv_editor_lua_history_step = R"lua(
local id, action = ...
local b = tonumber(id)
if vim.api.nvim_buf_is_loaded(b) then
    vim.api.nvim_buf_call(b, function() vim.cmd('silent ' .. action) end)
    return vim.bo[b].modified
end
)lua";

// The directory change can fail. Remember the old cwd until the shell applies
// the project so a failed or unanswered request can restore it without losing buffers.
constexpr const char *rv_editor_lua_switch_root = R"lua(
local root, discard = ...
if discard == '' then
    for _, b in ipairs(vim.api.nvim_list_bufs()) do
        if vim.api.nvim_buf_is_loaded(b) and vim.bo[b].modified then
            return 'a buffer still has unsaved changes'
        end
    end
end
_G.rv_project_previous_dir = vim.fn.getcwd()
vim.cmd('noautocmd call chdir(' .. vim.fn.string(root) .. ')')
return ''
)lua";

constexpr const char *rv_editor_lua_restore_root = R"lua(
local previous = _G.rv_project_previous_dir
if previous ~= nil then
    vim.cmd('noautocmd call chdir(' .. vim.fn.string(previous) .. ')')
    _G.rv_project_previous_dir = nil
end
)lua";

constexpr const char *rv_editor_lua_finish_switch_root = R"lua(
local discard = ... ~= ''
_G.rv_project_previous_dir = nil
for _, w in ipairs(vim.api.nvim_list_wins()) do
    if discard or not vim.bo[vim.api.nvim_win_get_buf(w)].modified then
        vim.api.nvim_win_call(w, function() vim.cmd(discard and 'enew!' or 'enew') end)
    end
end
for _, b in ipairs(vim.api.nvim_list_bufs()) do
    if vim.fn.bufwinid(b) == -1 then pcall(vim.api.nvim_buf_delete, b, {force = discard}) end
end
)lua";

// nvim's error reply is [type, message].
std::string rv_editor_reply_error(const rv_editor_mpack &error)
{
    if (error.is(mtype::array) && error.items.size() >= error_array_min_size) {
        return error.items[error_message_index].s;
    }
    return error.s.empty() ? "nvim refused the request" : error.s;
}

std::vector<rv_editor_nvim_saved> rv_editor_saved_list(const rv_editor_mpack &result)
{
    std::vector<rv_editor_nvim_saved> out;
    for (const rv_editor_mpack &m : result.items) {
        rv_editor_nvim_saved s;
        if (const rv_editor_mpack *v = m.get(buf_result_key_id)) {
            s.id = v->i;
        }
        if (const rv_editor_mpack *v = m.get(buf_result_key_name)) {
            s.name = v->s;
        }
        if (const rv_editor_mpack *v = m.get(buf_result_key_ok)) {
            s.ok = v->b;
        }
        if (const rv_editor_mpack *v = m.get(buf_result_key_error)) {
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

void rv_editor_nvim::check_modified(std::function<void(bool dirty, const std::string &failure)> done)
{
    if (!running()) {
        done(false, rv_editor_text("nvim_rpc.not_running"));
        return;
    }
    exec_lua(
        "return _G.rv_buffer_state()",
        {},
        [this, done = std::move(done)](const rv_editor_mpack &error, const rv_editor_mpack &result) {
            if (!error.is(mtype::nil)) {
                done(false, rv_editor_reply_error(error));
            } else if (!read_buffer_state(result)) {
                done(false, rv_editor_text("nvim_rpc.bad_buffer_state"));
            } else {
                done(std::ranges::any_of(buffers_, &rv_editor_nvim_buffer::modified), {});
            }
        },
        rv_editor_state_reply_timeout);
}

void rv_editor_nvim::save(const std::vector<int64_t> &ids, rv_editor_nvim_save_done done)
{
    if (!running()) {
        done({}, rv_editor_text("nvim_rpc.not_running"));
        return;
    }
    std::string list;
    for (const int64_t id : ids) {
        list += (list.empty() ? "" : buf_id_separator) + std::to_string(id);
    }
    exec_lua(rv_editor_lua_save, { list }, rv_editor_save_reply(std::move(done)));
}

void rv_editor_nvim::save_as(int64_t id, const std::filesystem::path &path, rv_editor_nvim_save_done done)
{
    if (!running()) {
        done({}, rv_editor_text("nvim_rpc.not_running"));
        return;
    }
    exec_lua(rv_editor_lua_save_as, { std::to_string(id), path.string() }, rv_editor_save_reply(std::move(done)));
}

void rv_editor_nvim::history(int64_t buffer, std::function<void(const rv_editor_nvim_history &)> done)
{
    exec_lua(rv_editor_lua_history,
        { std::to_string(buffer) },
        [done = std::move(done)](const rv_editor_mpack &error, const rv_editor_mpack &result) {
            if (error.is(mtype::nil) && result.items.size() == 2) {
                done({ result.items.front().b, result.items.back().b });
            } else {
                done({});
            }
        });
}

void rv_editor_nvim::history_step(int64_t buffer, bool redo)
{
    if (switching_root_) {
        return;
    }
    ++input_generation_;
    exec_lua(rv_editor_lua_history_step,
        { std::to_string(buffer), redo ? "redo" : "undo" },
        [this, buffer](const rv_editor_mpack &error, const rv_editor_mpack &result) {
            if (!error.is(mtype::nil) || !result.is(mtype::boolean)) {
                return;
            }
            // A background buffer need not emit BufModifiedSet until it gets focus.
            const auto found = std::ranges::find(buffers_, buffer, &rv_editor_nvim_buffer::id);
            if (found != buffers_.end()) {
                found->modified = result.b;
            }
        });
}

void rv_editor_nvim::switch_root(const std::filesystem::path &root, bool discard, std::function<void(const std::string &)> done)
{
    if (!running()) {
        done({});
        return;
    }
    switching_root_ = true;
    exec_lua(
        rv_editor_lua_switch_root,
        { root.string(), discard ? "discard" : "" },
        [done = std::move(done)](const rv_editor_mpack &error, const rv_editor_mpack &result) {
            if (!error.is(mtype::nil)) {
                done(rv_editor_reply_error(error));
            } else {
                done(result.is(mtype::string) ? result.s : rv_editor_text("shell_save.nvim_not_said"));
            }
        },
        rv_editor_state_reply_timeout);
}

void rv_editor_nvim::finish_switch_root(bool discard)
{
    if (running()) {
        held_.clear();
        exec_lua(rv_editor_lua_finish_switch_root,
            { discard ? "discard" : "" },
            [this](const rv_editor_mpack &error, const rv_editor_mpack &) {
                // A Lua error still means the cleanup request has finished in the committed directory.
                if (error.is(mtype::nil) || error.is(mtype::array)) {
                    switching_root_ = false;
                }
            });
    } else if (discard) {
        this->discard(0);
    }
}

void rv_editor_nvim::restore_root(std::function<void(const std::string &)> done)
{
    exec_lua(
        rv_editor_lua_restore_root,
        {},
        [this, done = std::move(done)](const rv_editor_mpack &error, const rv_editor_mpack &) {
            if (error.is(mtype::nil)) {
                switching_root_ = false;
            } else if (error.is(mtype::string) && error.s == rv_editor_text("nvim_rpc.reply_timeout")) {
                // Report the timeout to the shell, but keep Code paused until a queued rollback is confirmed.
                exec_lua(rv_editor_lua_restore_root, {}, [this](const rv_editor_mpack &late_error, const rv_editor_mpack &) {
                    if (late_error.is(mtype::nil)) {
                        switching_root_ = false;
                    }
                });
            }
            done(error.is(mtype::nil) ? std::string{} : rv_editor_reply_error(error));
        },
        rv_editor_state_reply_timeout);
}

} // namespace rv_editor
