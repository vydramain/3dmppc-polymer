// The code editor's nvim: start, attach, one window per code tile, buffers.

#include "nvim/rv_editor_nvim.hpp"

#include <cstdlib>
#include <sstream>
#include <system_error>

namespace rv_editor
{

namespace
{

using mtype = rv_editor_mpack::rv_editor_mpack_type;

// nvim from PATH: it is the user's own editor, not one this repository ships.
std::filesystem::path rv_editor_nvim_find()
{
    const char *path = std::getenv("PATH");
    if (path == nullptr) {
        return {};
    }
    std::stringstream dirs(path);
    std::string dir;
    while (std::getline(dirs, dir, ':')) {
        std::error_code ec;
        const std::filesystem::path p = std::filesystem::path(dir.empty() ? "." : dir) / "nvim";
        const auto st = std::filesystem::status(p, ec);
        if (!ec && std::filesystem::is_regular_file(st) &&
            (st.permissions() & std::filesystem::perms::owner_exec) != std::filesystem::perms::none) {
            return p;
        }
    }
    return {};
}

std::string rv_editor_args(const std::function<void(rv_editor_mpack_writer &)> &fill)
{
    std::string out;
    rv_editor_mpack_writer w(out);
    fill(w);
    return out;
}

} // namespace

bool rv_editor_nvim::ensure_started(const std::filesystem::path &cwd, rv_editor_log &log)
{
    if (started_ || !problem.empty()) {
        return started_;
    }
    const std::filesystem::path nvim = rv_editor_nvim_find();
    if (nvim.empty()) {
        problem = "nvim is not installed or not on PATH; the code editor needs it";
        return false;
    }
    const std::vector<std::string> argv = { nvim.string(), "--embed", "-u", RV_EDITOR_NVIM_CONFIG, "-i", "NONE" };
    std::string error;
    if (!rpc_.start(argv, cwd, error)) {
        problem = "cannot start nvim: " + error;
        return false;
    }
    started_ = true;
    attached_ = false;
    windows_.clear();
    asked_.clear();
    spare_.clear();
    buffers_.clear();
    sizes_.clear();
    shown_.clear();
    switching_ = 0;
    held_.clear();
    screen_ = {};
    log.add(rv_editor_log_source::editor, rv_editor_log_level::info, "code editor: " + nvim.string() + " --embed");
    attach();
    return true;
}

void rv_editor_nvim::attach()
{
    rpc_.request("nvim_ui_attach", rv_editor_args([](rv_editor_mpack_writer &w) {
        w.array(3);
        w.integer(120);
        w.integer(40);
        w.map(3);
        w.string("rgb");
        w.boolean(true);
        w.string("ext_linegrid");
        w.boolean(true);
        w.string("ext_multigrid");
        w.boolean(true);
    }),
        // Replies run later, in update(): only `this` is captured, and the code
        // tiles show `problem`.
        [this](const rv_editor_mpack &error, const rv_editor_mpack &) {
            if (!error.is(mtype::nil)) {
                problem = "nvim refused the UI: " + (error.items.size() > 1 ? error.items[1].s : std::string("?"));
                return;
            }
            attached_ = true;
        });
    // nvim's first window waits for the first code tile.
    rpc_.request("nvim_get_current_win", rv_editor_args([](rv_editor_mpack_writer &w) { w.array(0); }),
        [this](const rv_editor_mpack &error, const rv_editor_mpack &result) {
            if (error.is(mtype::nil)) {
                spare_.push_back(result.i);
            }
        });
}

void rv_editor_nvim::exec_lua(const std::string &code, const std::vector<std::string> &args,
    rv_editor_nvim_rpc::rv_editor_nvim_reply reply)
{
    rpc_.request("nvim_exec_lua", rv_editor_args([&](rv_editor_mpack_writer &w) {
        w.array(2);
        w.string(code);
        w.array(static_cast<uint32_t>(args.size()));
        for (const std::string &a : args) {
            w.string(a);
        }
    }),
        std::move(reply));
}

void rv_editor_nvim::update(rv_editor_log &log)
{
    if (!started_) {
        return;
    }
    std::string why;
    const bool alive = rpc_.poll([&](const std::string &method, const rv_editor_mpack &params) { notified(method, params, log); },
        why);
    const std::string err = rpc_.take_stderr();
    if (!err.empty()) {
        log.add(rv_editor_log_source::editor, rv_editor_log_level::warning, "nvim: " + err);
    }
    if (!alive) {
        started_ = false;
        attached_ = false;
        problem = why + ". Unsaved text is in nvim's swap files (:recover)";
        log.add(rv_editor_log_source::editor, rv_editor_log_level::error, "code editor: " + problem);
        rpc_.stop();
        return;
    }
    if (!held_.empty()) {
        input({});
    }
}

void rv_editor_nvim::notified(const std::string &method, const rv_editor_mpack &params, rv_editor_log &)
{
    if (method == "redraw") {
        screen_.apply(params);
        return;
    }
    if (method != "rv_buffers" || params.items.empty()) {
        return;
    }
    buffers_.clear();
    for (const rv_editor_mpack &b : params.items[0].items) {
        rv_editor_nvim_buffer buf;
        if (const rv_editor_mpack *v = b.get("id")) {
            buf.id = v->i;
        }
        if (const rv_editor_mpack *v = b.get("name")) {
            buf.name = v->s;
        }
        if (const rv_editor_mpack *v = b.get("modified")) {
            buf.modified = v->b;
        }
        if (const rv_editor_mpack *v = b.get("windows")) {
            for (const rv_editor_mpack &w : v->items) {
                buf.windows.push_back(w.i);
            }
        }
        buffers_.push_back(std::move(buf));
    }
}

int64_t rv_editor_nvim::window_for(uint32_t pane)
{
    const auto it = windows_.find(pane);
    if (it != windows_.end()) {
        // A window the user closed inside nvim (:q) is gone for good: the pane
        // asks for a new one instead of drawing nothing.
        const int64_t win = it->second;
        if (screen_.grid_of_window(win) != 0) {
            shown_.insert(win);
            return win;
        }
        if (shown_.count(win) == 0) {
            return win;
        }
        shown_.erase(win);
        sizes_.erase(win);
        windows_.erase(it);
    }
    if (!running() || !attached_) {
        return 0;
    }
    if (!spare_.empty()) {
        windows_[pane] = spare_.back();
        spare_.pop_back();
        return windows_[pane];
    }
    if (asked_[pane]) {
        return 0;
    }
    asked_[pane] = true;
    // A new window of its own; where nvim puts it does not matter, the tile
    // draws its grid (0005).
    rpc_.request("nvim_open_win", rv_editor_args([](rv_editor_mpack_writer &w) {
        w.array(3);
        w.integer(0);
        w.boolean(false);
        w.map(2);
        w.string("split");
        w.string("below");
        w.string("win");
        w.integer(-1);
    }),
        [this, pane](const rv_editor_mpack &error, const rv_editor_mpack &result) {
            asked_.erase(pane);
            if (error.is(mtype::nil)) {
                windows_[pane] = result.i;
            }
        });
    return 0;
}

void rv_editor_nvim::release(uint32_t pane)
{
    const auto it = windows_.find(pane);
    if (it == windows_.end()) {
        return;
    }
    const int64_t win = it->second;
    windows_.erase(it);
    sizes_.erase(win);
    if (windows_.empty()) {
        // nvim cannot close its last window; it waits for the next code tile.
        spare_.push_back(win);
        return;
    }
    rpc_.request("nvim_win_close", rv_editor_args([win](rv_editor_mpack_writer &w) {
        w.array(2);
        w.integer(win);
        w.boolean(true);
    }));
}

void rv_editor_nvim::resize(int64_t win, int32_t cols, int32_t rows)
{
    if (!running() || win == 0 || cols < 1 || rows < 1) {
        return;
    }
    const int32_t grid = screen_.grid_of_window(win);
    const auto want = std::make_pair(cols, rows);
    if (grid == 0 || sizes_[win] == want) {
        return;
    }
    sizes_[win] = want;
    rpc_.request("nvim_ui_try_resize_grid", rv_editor_args([&](rv_editor_mpack_writer &w) {
        w.array(3);
        w.integer(grid);
        w.integer(cols);
        w.integer(rows);
    }));
    // Grid 1 carries the command line: as wide as the tile being typed in.
    rpc_.request("nvim_ui_try_resize", rv_editor_args([&](rv_editor_mpack_writer &w) {
        w.array(2);
        w.integer(cols);
        w.integer(std::max(rows, 3));
    }));
}

void rv_editor_nvim::focus(int64_t win)
{
    // Only a real switch: the cursor already in this window's grid needs none.
    if (!running() || win == 0 || screen_.cursor_grid() == screen_.grid_of_window(win)) {
        return;
    }
    rpc_.notify("nvim_set_current_win", rv_editor_args([win](rv_editor_mpack_writer &w) {
        w.array(1);
        w.integer(win);
    }));
    switching_ = win;
    switch_at_ = std::chrono::steady_clock::now();
}

void rv_editor_nvim::mouse(int32_t grid, const char *action, int32_t row, int32_t col)
{
    if (!running()) {
        return;
    }
    rpc_.notify("nvim_input_mouse", rv_editor_args([&](rv_editor_mpack_writer &w) {
        w.array(6);
        w.string("left");
        w.string(action);
        w.string("");
        w.integer(grid);
        w.integer(row);
        w.integer(col);
    }));
}

void rv_editor_nvim::input(const std::string &keys)
{
    if (!running()) {
        return;
    }
    held_ += keys;
    if (switching_ != 0) {
        const bool arrived = screen_.cursor_grid() == screen_.grid_of_window(switching_);
        const bool late = std::chrono::steady_clock::now() - switch_at_ > std::chrono::milliseconds(300);
        if (!arrived && !late) {
            return;
        }
        switching_ = 0;
    }
    if (held_.empty()) {
        return;
    }
    rpc_.notify("nvim_input", rv_editor_args([&](rv_editor_mpack_writer &w) {
        w.array(1);
        w.string(held_);
    }));
    held_.clear();
}

void rv_editor_nvim::open(int64_t win, const std::filesystem::path &path, int32_t line)
{
    if (!running() || win == 0) {
        return;
    }
    exec_lua("local win, path, line = ...; win = tonumber(win); line = tonumber(line)\n"
             "vim.api.nvim_set_current_win(win)\n"
             "vim.cmd.edit(vim.fn.fnameescape(path))\n"
             "if line > 0 then pcall(vim.api.nvim_win_set_cursor, win, { line, 0 }) end",
        { std::to_string(win), path.string(), std::to_string(line) });
}

void rv_editor_nvim::checktime()
{
    if (running()) {
        exec_lua("vim.cmd('silent! checktime')", {});
    }
}

std::vector<rv_editor_nvim_buffer> rv_editor_nvim::modified() const
{
    std::vector<rv_editor_nvim_buffer> out;
    for (const rv_editor_nvim_buffer &b : buffers_) {
        if (b.modified) {
            out.push_back(b);
        }
    }
    return out;
}

bool rv_editor_nvim::modified_only_in(int64_t win) const
{
    for (const rv_editor_nvim_buffer &b : buffers_) {
        if (b.modified && b.windows.size() == 1 && b.windows[0] == win) {
            return true;
        }
    }
    return false;
}

void rv_editor_nvim::discard(int64_t win)
{
    if (!running()) {
        return;
    }
    if (win == 0) {
        exec_lua("for _, b in ipairs(vim.api.nvim_list_bufs()) do\n"
                 "  if vim.bo[b].modified then pcall(vim.api.nvim_buf_delete, b, { force = true }) end\n"
                 "end",
            {});
        return;
    }
    exec_lua("local win = tonumber(...); local b = vim.api.nvim_win_get_buf(win)\n"
             "vim.api.nvim_win_call(win, function() vim.cmd('enew') end)\n"
             "pcall(vim.api.nvim_buf_delete, b, { force = true })",
        { std::to_string(win) });
}

const rv_editor_nvim_buffer *rv_editor_nvim::buffer_in(int64_t win) const
{
    for (const rv_editor_nvim_buffer &b : buffers_) {
        for (const int64_t w : b.windows) {
            if (w == win) {
                return &b;
            }
        }
    }
    return nullptr;
}

std::vector<uint32_t> rv_editor_nvim::panes() const
{
    std::vector<uint32_t> out;
    for (const auto &entry : windows_) {
        out.push_back(entry.first);
    }
    return out;
}

void rv_editor_nvim::stop()
{
    if (started_ && rpc_.running()) {
        // Queued after any :write the user asked for, so those run first.
        rpc_.notify("nvim_command", rv_editor_args([](rv_editor_mpack_writer &w) {
            w.array(1);
            w.string("qa!");
        }));
    }
    if (started_) {
        rpc_.stop();
    }
    started_ = false;
    attached_ = false;
}

} // namespace rv_editor
