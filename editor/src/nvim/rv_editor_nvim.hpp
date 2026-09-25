#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "log/rv_editor_log.hpp"
#include "nvim/rv_editor_nvim_grid.hpp"
#include "nvim/rv_editor_nvim_rpc.hpp"

namespace rv_editor
{

// One buffer nvim holds, as the editor's config reports it after every change.
struct rv_editor_nvim_buffer
{
    int64_t id = 0;
    std::string name;             // full path, empty for a new unnamed buffer
    bool modified = false;
    std::vector<int64_t> windows; // windows showing it
};

// What one buffer's save came to, as nvim reports it after the write.
struct rv_editor_nvim_saved
{
    int64_t id = 0;    // 0: nvim itself did not answer
    std::string name;  // full path; empty for an Untitled buffer
    bool ok = false;
    std::string error; // nvim's message when not ok
};

// The outcome of a save: one entry per buffer, and `failure` when nvim did not
// answer at all.
using rv_editor_nvim_save_done =
    std::function<void(const std::vector<rv_editor_nvim_saved> &saved, const std::string &failure)>;

// The code editor (docs/adr/0005-code-editor-nvim.md): one `nvim --embed`
// per editor window, started with the first code tile. Each code tile is one
// nvim window whose grid the tile draws; nvim keeps the text, the undo, the
// swap files and the modified flags, and this client mirrors what it needs.
class rv_editor_nvim
{
public:
    // Why the code editor cannot run, or empty. Checked before starting.
    std::string problem;

    // Starts nvim once, with the editor's config, in `cwd`. False with the
    // reason in `problem` (ARC-05: only the code tiles lose, nothing else).
    bool ensure_started(const std::filesystem::path &cwd, rv_editor_log &log);
    bool running() const { return started_ && rpc_.running(); }

    // Once a frame: applies redraws and answers. Returns false the frame nvim ends.
    void update(rv_editor_log &log);

    // The window that shows code pane `pane`, created on first use; 0 until nvim
    // has answered.
    int64_t window_for(uint32_t pane);
    // The pane gave its window back: the window closes, or waits for the next
    // code pane when it is nvim's last.
    void release(uint32_t pane);

    // Resizes the window's grid to the tile, in cells.
    void resize(int64_t win, int32_t cols, int32_t rows);
    void focus(int64_t win);
    // Keys in nvim_input notation ("<C-s>", "a", "<lt>").
    void input(const std::string &keys);
    // A mouse event in `grid` at cell (row, col), as nvim_input_mouse takes it:
    // button "left" with "press", "drag" or "release", or "wheel" with "up",
    // "down", "left" or "right".
    void mouse(const char *button, const char *action, int32_t grid, int32_t row, int32_t col);
    // Opens `path` in `win`, at `line` when it is above 0.
    void open(int64_t win, const std::filesystem::path &path, int32_t line);
    // `:checktime`: re-reads buffers changed on disk (PRJ-06/PRJ-07 go through
    // nvim's autoread and its own changed-file question).
    void checktime();

    // Buffers with unsaved changes, and those only `win` shows.
    std::vector<rv_editor_nvim_buffer> modified() const;
    bool modified_only_in(int64_t win) const;
    // Writes the modified buffers `ids`, or every modified one when `ids` is
    // empty, and reports each to `done` once nvim has answered. A buffer counts
    // as saved only when its write returned and nvim no longer marks it
    // modified; an Untitled one fails with "no file name". Nothing is dropped.
    void save(const std::vector<int64_t> &ids, rv_editor_nvim_save_done done);
    // Writes buffer `id` under `path` (`:saveas`, which never replaces an
    // existing file) and reports like save().
    void save_as(int64_t id, const std::filesystem::path &path, rv_editor_nvim_save_done done);
    // Discard, on the user's word only: `:bdelete!` for the buffer `win` shows,
    // or for every modified buffer when `win` is 0.
    void discard(int64_t win);
    // Another project: every window gets an empty buffer, the old buffers go,
    // and nvim's directory becomes `root`. Refused, with the reason in `done`,
    // while any buffer is modified.
    void switch_root(const std::filesystem::path &root, std::function<void(const std::string &failure)> done);

    // Full Vim (normal mode, Vim keys) instead of the ordinary editor: the same
    // switch as F2 in a code tile, which it follows.
    bool vim_mode() const { return vim_mode_; }
    void toggle_vim_mode();

    // The buffer `win` shows, or nullptr before nvim has reported it.
    const rv_editor_nvim_buffer *buffer_in(int64_t win) const;

    // Code panes that hold a window now.
    std::vector<uint32_t> panes() const;

    const rv_editor_nvim_screen &screen() const { return screen_; }
    // Ends nvim after the user answered for its modified buffers: what was
    // saved is saved, the rest is dropped with its swap files (`:qa!`). A crash
    // of the editor instead leaves the swap files for `:recover`.
    void stop();

private:
    void attach();
    void notified(const std::string &method, const rv_editor_mpack &params, rv_editor_log &log);
    void exec_lua(const std::string &code, const std::vector<std::string> &args, rv_editor_nvim_rpc::rv_editor_nvim_reply reply = {});

    rv_editor_nvim_rpc rpc_;
    rv_editor_nvim_screen screen_;
    bool started_ = false;
    bool attached_ = false;
    std::map<uint32_t, int64_t> windows_; // code pane -> nvim window
    std::map<uint32_t, bool> asked_;      // a window was requested for this pane
    std::vector<int64_t> spare_;          // windows no pane shows, the first is nvim's own
    std::vector<rv_editor_nvim_buffer> buffers_;
    bool vim_mode_ = false;
    std::map<int64_t, std::pair<int32_t, int32_t>> sizes_;
    std::set<int64_t> shown_;             // windows whose grid has been seen
    // Keys typed right after a window switch wait until nvim has made it, since
    // nvim_input overtakes nvim_set_current_win (a "fast" call).
    int64_t switching_ = 0;
    std::chrono::steady_clock::time_point switch_at_{};
    std::string held_;
};

} // namespace rv_editor
