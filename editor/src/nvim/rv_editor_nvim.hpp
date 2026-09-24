#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
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
    // Opens `path` in `win`, at `line` when it is above 0.
    void open(int64_t win, const std::filesystem::path &path, int32_t line);
    // `:checktime`: re-reads buffers changed on disk (PRJ-06/PRJ-07 go through
    // nvim's autoread and its own changed-file question).
    void checktime();

    // Buffers with unsaved changes, and those only `win` shows.
    std::vector<rv_editor_nvim_buffer> modified() const;
    bool modified_only_in(int64_t win) const;
    // Save, Discard: `:write` or `:bdelete!` for the buffer `win` shows, or
    // for every modified buffer when `win` is 0.
    void save(int64_t win);
    void discard(int64_t win);

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
    std::map<int64_t, std::pair<int32_t, int32_t>> sizes_;
    std::set<int64_t> shown_;             // windows whose grid has been seen
    // Keys typed right after a window switch wait until nvim has made it, since
    // nvim_input overtakes nvim_set_current_win (a "fast" call).
    int64_t switching_ = 0;
    std::chrono::steady_clock::time_point switch_at_{};
    std::string held_;
};

} // namespace rv_editor
