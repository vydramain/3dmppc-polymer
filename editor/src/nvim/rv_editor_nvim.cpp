// The code editor's nvim: start, attach, one window per code tile, buffers.

#include "nvim/rv_editor_nvim.hpp"

#include <cstdlib>
#include <sstream>
#include <system_error>

#include "pdk/rv_err.h"

namespace rv_editor
{

namespace
{

using mtype = rv_editor_mpack::rv_editor_mpack_type;

// nvim UI attachment protocol: defaults and msgpack structure sizes
constexpr int32_t ui_default_width_cols = 120; // nvim_ui_attach default width
constexpr int32_t ui_default_height_rows = 40; // nvim_ui_attach default height
constexpr size_t ui_attach_args_count = 3;     // nvim_ui_attach: [width, height, options]
constexpr size_t ui_attach_options_count = 3;  // ui options map: rgb, ext_linegrid, ext_multigrid

// nvim RPC protocol message sizes
constexpr size_t exec_lua_args_count = 2;           // nvim_exec_lua: [code, args]
constexpr size_t open_win_args_count = 3;           // nvim_open_win: [buffer, enter, opts]
constexpr size_t open_win_options_count = 2;        // open_win opts: split, win
constexpr size_t win_close_args_count = 2;          // nvim_win_close: [window, force]
constexpr size_t ui_try_resize_grid_args_count = 3; // nvim_ui_try_resize_grid: [grid, cols, rows]
constexpr size_t ui_try_resize_args_count = 2;      // nvim_ui_try_resize: [cols, rows]
constexpr size_t input_mouse_args_count = 6;        // nvim_input_mouse: [button, action, mod, grid, row, col]

// UI constraints and timing
constexpr int32_t cmdline_min_rows = 3;           // grid 1 (command line) minimum height
constexpr int32_t window_switch_timeout_ms = 300; // wait for cursor after window switch

// nvim RPC method names (nvim_msg_out protocol)
constexpr std::string_view nvim_prog = "nvim";                                 // program name from PATH
constexpr std::string_view rpc_ui_attach = "nvim_ui_attach";                   // attach UI to nvim
constexpr std::string_view rpc_get_current_win = "nvim_get_current_win";       // get active window
constexpr std::string_view rpc_exec_lua = "nvim_exec_lua";                     // execute Lua code
constexpr std::string_view rpc_open_win = "nvim_open_win";                     // create window
constexpr std::string_view rpc_win_close = "nvim_win_close";                   // close window
constexpr std::string_view rpc_ui_try_resize_grid = "nvim_ui_try_resize_grid"; // resize grid
constexpr std::string_view rpc_ui_try_resize = "nvim_ui_try_resize";           // resize UI
constexpr std::string_view rpc_set_current_win = "nvim_set_current_win";       // switch window
constexpr std::string_view rpc_input_mouse = "nvim_input_mouse";               // mouse event
constexpr std::string_view rpc_input = "nvim_input";                           // key input
constexpr std::string_view rpc_command = "nvim_command";                       // execute command

// UI option names for nvim_ui_attach
constexpr std::string_view ui_opt_rgb = "rgb";                     // true color support
constexpr std::string_view ui_opt_ext_linegrid = "ext_linegrid";   // line grid extension
constexpr std::string_view ui_opt_ext_multigrid = "ext_multigrid"; // multi-grid extension

// nvim_open_win option keys and values
constexpr std::string_view open_win_opt_split = "split";   // split direction
constexpr std::string_view open_win_split_below = "below"; // split below current
constexpr std::string_view open_win_opt_win = "win";       // window option key

// nvim startup and command names
constexpr std::string_view nvim_opt_embed = "--embed";  // embed mode flag
constexpr std::string_view nvim_opt_cmd = "--cmd";      // pre-startup command
constexpr std::string_view nvim_opt_init_file = "-u";   // init file to use (NONE = no init)
constexpr std::string_view nvim_opt_shada_file = "-i";  // shada (viminfo) file or NONE
constexpr std::string_view nvim_init_none = "NONE";     // init file value: none
constexpr std::string_view nvim_cmd_quit_force = "qa!"; // quit all windows, force

// LSP server names for language detection
constexpr std::string_view lsp_clangd = "clangd"; // C/C++ language server
constexpr std::string_view lsp_luals = "luals";   // Lua language server

// File extensions for language detection
constexpr std::string_view ext_c = ".c";     // C source
constexpr std::string_view ext_h = ".h";     // C header
constexpr std::string_view ext_cc = ".cc";   // C++ source
constexpr std::string_view ext_hh = ".hh";   // C++ header
constexpr std::string_view ext_cpp = ".cpp"; // C++ source
constexpr std::string_view ext_hpp = ".hpp"; // C++ header
constexpr std::string_view ext_cxx = ".cxx"; // C++ source
constexpr std::string_view ext_hxx = ".hxx"; // C++ header
constexpr std::string_view ext_lua = ".lua"; // Lua source

// Vim and Lua code fragments
constexpr std::string_view vim_cmd_palette_prefix = "let g:rv_editor_palette='"; // nvim palette var prefix
constexpr std::string_view lua_toggle_vim_mode = "_G.rv_toggle_vim_mode()";      // toggle mode, defined in rv_editor_init.lua
constexpr std::string_view lua_checktime = "vim.cmd('silent! checktime')";       // check file changes on disk
constexpr std::string_view lua_delete_modified_bufs =
    "for _, b in ipairs(vim.api.nvim_list_bufs()) do\n"
    "  if vim.bo[b].modified then pcall(vim.api.nvim_buf_delete, b, { force = true }) end\n"
    "end"; // delete modified buffers
constexpr std::string_view lua_close_and_delete_buf =
    "local win = tonumber(...); local b = vim.api.nvim_win_get_buf(win)\n"
    "vim.api.nvim_win_call(win, function() vim.cmd('enew') end)\n"
    "pcall(vim.api.nvim_buf_delete, b, { force = true })"; // close window, replace with new buffer, delete old

std::string rv_editor_args(const std::function<void(rv_editor_mpack_writer &)> &fill)
{
    std::string out;
    rv_editor_mpack_writer w(out);
    fill(w);
    return out;
}

} // namespace

int rv_editor_nvim::ensure_started(const std::filesystem::path &cwd, rv_editor_log &log)
{
    if (started_) {
        return RV_OK;
    }
    if (!problem.empty()) {
        return last_error_;
    }
    // From PATH: it is the user's own editor, not one this repository ships.
    const std::filesystem::path nvim = rv_editor_process_find(nvim_prog.data());
    if (nvim.empty()) {
        problem = "nvim is not installed or not on PATH; the code editor needs it";
        last_error_ = RV_ERR_NOENT;
        return RV_ERR_NOENT;
    }
    const std::vector<std::string> argv = { nvim.string(),
        std::string(nvim_opt_embed),
        std::string(nvim_opt_cmd),
        std::string(vim_cmd_palette_prefix) + std::string(RV_EDITOR_NVIM_PALETTE) + "'",
        std::string(nvim_opt_init_file),
        RV_EDITOR_NVIM_CONFIG,
        std::string(nvim_opt_shada_file),
        std::string(nvim_init_none) };
    std::string error;
    const int err = rpc_.start(argv, cwd, error);
    if (err != RV_OK) {
        problem = "cannot start nvim: " + error;
        last_error_ = err;
        return err;
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
    return RV_OK;
}

void rv_editor_nvim::attach()
{
    rpc_.request(rpc_ui_attach.data(),
        rv_editor_args([](rv_editor_mpack_writer &w) {
            w.array(ui_attach_args_count);
            w.integer(ui_default_width_cols);
            w.integer(ui_default_height_rows);
            w.map(ui_attach_options_count);
            w.string(ui_opt_rgb.data());
            w.boolean(true);
            w.string(ui_opt_ext_linegrid.data());
            w.boolean(true);
            w.string(ui_opt_ext_multigrid.data());
            w.boolean(true);
        }),
        // Replies run later, in update(): only `this` is captured, and the code
        // tiles show `problem`.
        [this](const rv_editor_mpack &error, const rv_editor_mpack &) {
            if (!error.is(mtype::nil)) {
                problem = "nvim refused the UI: " + (error.items.size() > 1 ? error.items[1].s : std::string("?"));
                last_error_ = RV_ERR_IO;
                return;
            }
            attached_ = true;
        });
    // nvim's first window waits for the first code tile.
    rpc_.request(rpc_get_current_win.data(),
        rv_editor_args([](rv_editor_mpack_writer &w) {
            w.array(0);
        }),
        [this](const rv_editor_mpack &error, const rv_editor_mpack &result) {
            if (error.is(mtype::nil)) {
                spare_.push_back(result.i);
            }
        });
}

void rv_editor_nvim::exec_lua(const std::string &code,
    const std::vector<std::string> &args,
    rv_editor_nvim_rpc::rv_editor_nvim_reply reply)
{
    rpc_.request(rpc_exec_lua.data(),
        rv_editor_args([&](rv_editor_mpack_writer &w) {
            w.array(exec_lua_args_count);
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
    const bool alive = rpc_.poll(
        [&](const std::string &method, const rv_editor_mpack &params) {
            notified(method, params, log);
        },
        why);
    const std::string err = rpc_.take_stderr();
    if (!err.empty()) {
        log.add(rv_editor_log_source::editor, rv_editor_log_level::warning, "nvim: " + err);
    }
    if (!alive) {
        started_ = false;
        attached_ = false;
        problem = why + ". Unsaved text is in nvim's swap files (:recover)";
        last_error_ = RV_ERR_IO;
        log.add(rv_editor_log_source::editor, rv_editor_log_level::error, "code editor: " + problem);
        rpc_.stop();
        return;
    }
    if (!held_.empty()) {
        input({});
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
    rpc_.request(rpc_open_win.data(),
        rv_editor_args([](rv_editor_mpack_writer &w) {
            w.array(open_win_args_count);
            w.integer(0);
            w.boolean(false);
            w.map(open_win_options_count);
            w.string(open_win_opt_split.data());
            w.string(open_win_split_below.data());
            w.string(open_win_opt_win.data());
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
    rpc_.request(rpc_win_close.data(), rv_editor_args([win](rv_editor_mpack_writer &w) {
        w.array(win_close_args_count);
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
    rpc_.request(rpc_ui_try_resize_grid.data(), rv_editor_args([&](rv_editor_mpack_writer &w) {
        w.array(ui_try_resize_grid_args_count);
        w.integer(grid);
        w.integer(cols);
        w.integer(rows);
    }));
    // Grid 1 carries the command line: as wide as the tile being typed in.
    rpc_.request(rpc_ui_try_resize.data(), rv_editor_args([&](rv_editor_mpack_writer &w) {
        w.array(ui_try_resize_args_count);
        w.integer(cols);
        w.integer(std::max(rows, cmdline_min_rows));
    }));
}

void rv_editor_nvim::focus(int64_t win)
{
    // Only a real switch: the cursor already in this window's grid needs none.
    // A cursor on the message grid or grid 1 is a prompt or command line: keys must reach it.
    if (!running() || win == 0) {
        return;
    }
    const int32_t cursor = screen_.cursor_grid();
    if (cursor == screen_.grid_of_window(win) || cursor == 1 || (cursor != 0 && cursor == screen_.message_grid())) {
        return;
    }
    rpc_.notify(rpc_set_current_win.data(), rv_editor_args([win](rv_editor_mpack_writer &w) {
        w.array(1);
        w.integer(win);
    }));
    switching_ = win;
    switch_at_ = std::chrono::steady_clock::now();
}

void rv_editor_nvim::mouse(const char *button, const char *action, int32_t grid, int32_t row, int32_t col)
{
    if (!running()) {
        return;
    }
    rpc_.notify(rpc_input_mouse.data(), rv_editor_args([&](rv_editor_mpack_writer &w) {
        w.array(input_mouse_args_count);
        w.string(button);
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
        const bool late = std::chrono::steady_clock::now() - switch_at_ > std::chrono::milliseconds(window_switch_timeout_ms);
        if (!arrived && !late) {
            return;
        }
        switching_ = 0;
    }
    if (held_.empty()) {
        return;
    }
    rpc_.notify(rpc_input.data(), rv_editor_args([&](rv_editor_mpack_writer &w) {
        w.array(1);
        w.string(held_);
    }));
    held_.clear();
}

void rv_editor_nvim::toggle_vim_mode()
{
    if (running()) {
        exec_lua(std::string(lua_toggle_vim_mode), {});
    }
}

void rv_editor_nvim::checktime()
{
    if (running()) {
        exec_lua(std::string(lua_checktime), {});
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
        exec_lua(std::string(lua_delete_modified_bufs), {});
        return;
    }
    exec_lua(std::string(lua_close_and_delete_buf), { std::to_string(win) });
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

std::string rv_editor_nvim::lsp_server_for(const std::string &path)
{
    const std::string ext = std::filesystem::path(path).extension().string();
    if (ext == ext_c || ext == ext_h || ext == ext_cc || ext == ext_hh || ext == ext_cpp || ext == ext_hpp || ext == ext_cxx ||
        ext == ext_hxx) {
        return std::string(lsp_clangd);
    }
    if (ext == ext_lua) {
        return std::string(lsp_luals);
    }
    return {};
}

const rv_editor_nvim_lsp *rv_editor_nvim::lsp_status(const std::string &server) const
{
    const auto it = lsp_.find(server);
    return it != lsp_.end() ? &it->second : nullptr;
}

const std::vector<rv_editor_nvim_diagnostic> &rv_editor_nvim::diagnostics_for(const std::string &path) const
{
    static const std::vector<rv_editor_nvim_diagnostic> empty;
    const auto it = diagnostics_.find(path);
    return it != diagnostics_.end() ? it->second : empty;
}

void rv_editor_nvim::stop()
{
    if (started_ && rpc_.running()) {
        // Queued after any :write the user asked for, so those run first.
        rpc_.notify(rpc_command.data(), rv_editor_args([](rv_editor_mpack_writer &w) {
            w.array(1);
            w.string(nvim_cmd_quit_force.data());
        }));
    }
    if (started_) {
        rpc_.stop();
    }
    started_ = false;
    attached_ = false;
}

} // namespace rv_editor
