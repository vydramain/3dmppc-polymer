#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "nvim/rv_editor_msgpack.hpp"

namespace rv_editor
{

// What nvim's `redraw` notifications describe, applied event by event
// (docs/adr/0005-code-editor-nvim.md): highlight attributes, the default
// colours, and one grid per window plus grid 1 with the command line.

// The cursor's shape in one mode, as nvim's mode_info_set gives it (guicursor).
enum class rv_editor_nvim_cursor_kind
{
    block,
    vertical,   // a bar at the cell's left
    horizontal, // a bar at the cell's bottom
};

struct rv_editor_nvim_cursor
{
    rv_editor_nvim_cursor_kind kind = rv_editor_nvim_cursor_kind::block;
    int32_t percent = 100; // of the cell's width (vertical) or height (horizontal)
};

struct rv_editor_nvim_attr
{
    uint32_t fg = 0;
    uint32_t bg = 0;
    bool has_fg = false;
    bool has_bg = false;
    bool reverse = false;
    bool bold = false;
    bool italic = false;
    bool underline = false;
};

struct rv_editor_nvim_cell
{
    std::string text; // one screen cell: a UTF-8 character, empty after a double-width one
    int32_t hl = 0;
};

struct rv_editor_nvim_grid
{
    int32_t width = 0;
    int32_t height = 0;
    std::vector<rv_editor_nvim_cell> cells; // row-major
    int32_t cursor_row = -1;
    int32_t cursor_col = -1;
    int64_t win = 0;      // the window it shows, 0 for grid 1 and message grids
    bool hidden = false;

    rv_editor_nvim_cell &at(int32_t row, int32_t col) { return cells[static_cast<size_t>(row * width + col)]; }
};

class rv_editor_nvim_screen
{
public:
    // One `redraw` notification's parameters: a list of [event, args...] batches.
    void apply(const rv_editor_mpack &batches);

    const rv_editor_nvim_grid *grid(int32_t id) const;
    // The grid showing window `win`, or 0.
    int32_t grid_of_window(int64_t win) const;

    // Colours of one cell, the reverse attribute applied.
    void colors(int32_t hl, uint32_t &fg, uint32_t &bg) const;
    const rv_editor_nvim_attr &attr(int32_t hl) const;

    int32_t cursor_grid() const { return cursor_grid_; }
    const std::string &mode() const { return mode_; }
    // The cursor's shape in the current mode; a block before nvim has said.
    rv_editor_nvim_cursor cursor_shape() const;
    // Grid of the message area (msg_set_pos), 0 when none is shown.
    int32_t message_grid() const { return msg_grid_; }
    int32_t message_row() const { return msg_row_; }
    // True after a flush: what was applied is complete and can be drawn.
    bool flushed() const { return flushed_; }

private:
    void event(const std::string &name, const rv_editor_mpack &args);
    void grid_line(const rv_editor_mpack &args);
    void grid_scroll(const rv_editor_mpack &args);
    void grid_resize(int32_t id, int32_t w, int32_t h);

    std::map<int32_t, rv_editor_nvim_grid> grids_;
    std::map<int32_t, rv_editor_nvim_attr> attrs_;
    rv_editor_nvim_attr default_;
    int32_t cursor_grid_ = 0;
    int32_t msg_grid_ = 0;
    int32_t msg_row_ = 0;
    std::string mode_;
    int32_t mode_index_ = -1;
    std::vector<rv_editor_nvim_cursor> mode_info_;
    bool flushed_ = false;
};

} // namespace rv_editor
