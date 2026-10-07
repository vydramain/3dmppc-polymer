// Redraw event handlers for nvim linegrid/multigrid events.

#include "nvim/rv_editor_nvim_grid.hpp"

#include <algorithm>

namespace rv_editor
{

namespace
{

using mtype = rv_editor_mpack::rv_editor_mpack_type;

// Minimum argument counts for nvim protocol events
constexpr size_t grid_resize_args = 3;
constexpr size_t grid_cursor_goto_args = 3;
constexpr size_t win_float_pos_args = 7;
constexpr size_t win_float_zindex_args = 8;
constexpr size_t win_pos_args = 4;
constexpr size_t msg_set_pos_args = 2;
constexpr size_t hl_attr_define_args = 2;
constexpr size_t mode_info_set_args = 2;
constexpr size_t mode_change_args = 2;

// grid_resize event argument positions
constexpr size_t grid_resize_grid = 0;
constexpr size_t grid_resize_width = 1;
constexpr size_t grid_resize_height = 2;

// grid_cursor_goto event argument positions
constexpr size_t grid_cursor_goto_grid = 0;
constexpr size_t grid_cursor_goto_row = 1;
constexpr size_t grid_cursor_goto_col = 2;

// win_float_pos event argument positions
constexpr size_t win_float_pos_grid = 0;
constexpr size_t win_float_pos_win = 1;
constexpr size_t win_float_pos_anchor = 2;
constexpr size_t win_float_pos_anchor_grid = 3;
constexpr size_t win_float_pos_anchor_row = 4;
constexpr size_t win_float_pos_anchor_col = 5;
constexpr size_t win_float_pos_zindex = 7;

// win_pos event argument positions
constexpr size_t win_pos_grid = 0;
constexpr size_t win_pos_win = 1;
constexpr size_t win_pos_start_row = 2;
constexpr size_t win_pos_start_col = 3;

// msg_set_pos uses grid_line indices (grid, row)
constexpr size_t grid_line_grid = 0;
constexpr size_t grid_line_row = 1;

// Protocol-defined constants
constexpr int32_t default_float_zindex = 50;
constexpr const char *empty_cell_text = " ";

// Keys in hl_attr_define event map from ui protocol
constexpr std::string_view hl_key_foreground = "foreground";
constexpr std::string_view hl_key_background = "background";
constexpr std::string_view hl_key_reverse = "reverse";
constexpr std::string_view hl_key_bold = "bold";
constexpr std::string_view hl_key_italic = "italic";
constexpr std::string_view hl_key_underline = "underline";
constexpr std::string_view hl_key_undercurl = "undercurl";

// Keys and values in mode_info_set event from ui protocol
constexpr std::string_view mode_key_cursor_shape = "cursor_shape";
constexpr std::string_view cursor_shape_vertical = "vertical";
constexpr std::string_view cursor_shape_horizontal = "horizontal";
constexpr std::string_view mode_key_cell_percentage = "cell_percentage";

// Minimum cursor fill percentage
constexpr int32_t cell_percentage_max = 100;

} // namespace

void rv_editor_nvim_screen::on_grid_resize(const rv_editor_mpack &args)
{
    if (args.items.size() < grid_resize_args) {
        return;
    }
    grid_resize(rv_editor_int(args.items[grid_resize_grid]),
        rv_editor_int(args.items[grid_resize_width]),
        rv_editor_int(args.items[grid_resize_height]));
}

void rv_editor_nvim_screen::on_grid_clear(const rv_editor_mpack &args)
{
    if (args.items.empty()) {
        return;
    }
    const auto it = grids_.find(rv_editor_int(args.items[grid_line_grid]));
    if (it == grids_.end()) {
        return;
    }
    std::fill(it->second.cells.begin(), it->second.cells.end(), rv_editor_nvim_cell{ empty_cell_text, 0 });
}

void rv_editor_nvim_screen::on_grid_cursor_goto(const rv_editor_mpack &args)
{
    if (args.items.size() < grid_cursor_goto_args) {
        return;
    }
    cursor_grid_ = rv_editor_int(args.items[grid_cursor_goto_grid]);
    rv_editor_nvim_grid &g = grids_[cursor_grid_];
    g.cursor_row = rv_editor_int(args.items[grid_cursor_goto_row]);
    g.cursor_col = rv_editor_int(args.items[grid_cursor_goto_col]);
}

void rv_editor_nvim_screen::on_grid_destroy(const rv_editor_mpack &args)
{
    if (args.items.empty()) {
        return;
    }
    grids_.erase(rv_editor_int(args.items[grid_line_grid]));
}

void rv_editor_nvim_screen::on_win_float_pos(const rv_editor_mpack &args)
{
    if (args.items.size() < win_float_pos_args) {
        return;
    }
    rv_editor_nvim_grid &g = grids_[rv_editor_int(args.items[win_float_pos_grid])];
    g.win = rv_editor_int(args.items[win_float_pos_win]);
    g.is_float = true;
    g.anchor = args.items[win_float_pos_anchor].s;
    g.anchor_grid = rv_editor_int(args.items[win_float_pos_anchor_grid]);
    // nvim sends row/col as floats; store as-is for rendering precision.
    g.anchor_row = rv_editor_real_or_int(args.items[win_float_pos_anchor_row]);
    g.anchor_col = rv_editor_real_or_int(args.items[win_float_pos_anchor_col]);
    g.zindex =
        args.items.size() >= win_float_zindex_args ? rv_editor_int(args.items[win_float_pos_zindex]) : default_float_zindex;
    g.hidden = false;
}

void rv_editor_nvim_screen::on_win_pos(const rv_editor_mpack &args)
{
    if (args.items.size() < win_pos_args) {
        return;
    }
    rv_editor_nvim_grid &g = grids_[rv_editor_int(args.items[win_pos_grid])];
    g.win = args.items[win_pos_win].i;
    g.is_float = false;
    g.hidden = false;
    g.window_row = rv_editor_real_or_int(args.items[win_pos_start_row]);
    g.window_col = rv_editor_real_or_int(args.items[win_pos_start_col]);
}

void rv_editor_nvim_screen::on_win_hidden(const rv_editor_mpack &args)
{
    // win_hide and win_close: the grid stays, hidden.
    if (args.items.empty()) {
        return;
    }
    const auto it = grids_.find(rv_editor_int(args.items[grid_line_grid]));
    if (it == grids_.end()) {
        return;
    }
    it->second.hidden = true;
}

void rv_editor_nvim_screen::on_msg_set_pos(const rv_editor_mpack &args)
{
    if (args.items.size() < msg_set_pos_args) {
        return;
    }
    msg_grid_ = rv_editor_int(args.items[grid_line_grid]);
    msg_row_ = rv_editor_int(args.items[grid_line_row]);
}

void rv_editor_nvim_screen::on_hl_attr_define(const rv_editor_mpack &args)
{
    if (args.items.size() < hl_attr_define_args) {
        return;
    }
    rv_editor_nvim_attr attr;
    const rv_editor_mpack &rgb = args.items[1];
    if (const rv_editor_mpack *v = rgb.get(hl_key_foreground)) {
        attr.fg = static_cast<uint32_t>(v->i);
        attr.has_fg = true;
    }
    if (const rv_editor_mpack *v = rgb.get(hl_key_background)) {
        attr.bg = static_cast<uint32_t>(v->i);
        attr.has_bg = true;
    }
    attr.reverse = rgb.get(hl_key_reverse) != nullptr && rgb.get(hl_key_reverse)->b;
    attr.bold = rgb.get(hl_key_bold) != nullptr && rgb.get(hl_key_bold)->b;
    attr.italic = rgb.get(hl_key_italic) != nullptr && rgb.get(hl_key_italic)->b;
    attr.underline = (rgb.get(hl_key_underline) != nullptr && rgb.get(hl_key_underline)->b) ||
        (rgb.get(hl_key_undercurl) != nullptr && rgb.get(hl_key_undercurl)->b);
    attrs_[rv_editor_int(args.items[0])] = attr;
}

void rv_editor_nvim_screen::on_default_colors_set(const rv_editor_mpack &args)
{
    if (args.items.size() < hl_attr_define_args) {
        return;
    }
    default_.fg = static_cast<uint32_t>(args.items[0].i);
    default_.bg = static_cast<uint32_t>(args.items[1].i);
    default_.has_fg = default_.has_bg = true;
}

void rv_editor_nvim_screen::on_mode_info_set(const rv_editor_mpack &args)
{
    if (args.items.size() < mode_info_set_args) {
        return;
    }
    mode_info_.clear();
    for (const rv_editor_mpack &m : args.items[1].items) {
        rv_editor_nvim_cursor c;
        if (const rv_editor_mpack *v = m.get(mode_key_cursor_shape)) {
            if (v->s == cursor_shape_vertical) {
                c.kind = rv_editor_nvim_cursor_kind::vertical;
            } else if (v->s == cursor_shape_horizontal) {
                c.kind = rv_editor_nvim_cursor_kind::horizontal;
            } else {
                c.kind = rv_editor_nvim_cursor_kind::block;
            }
        }
        if (const rv_editor_mpack *v = m.get(mode_key_cell_percentage); v != nullptr) {
            const bool percent_in_range = v->i > 0 && v->i <= cell_percentage_max;
            if (percent_in_range) {
                c.percent = static_cast<int32_t>(v->i);
            }
        }
        mode_info_.push_back(c);
    }
}

void rv_editor_nvim_screen::on_mode_change(const rv_editor_mpack &args)
{
    if (args.items.empty()) {
        return;
    }
    mode_ = args.items[0].s;
    mode_index_ = args.items.size() >= mode_change_args ? rv_editor_int(args.items[1]) : -1;
}

void rv_editor_nvim_screen::on_flush(const rv_editor_mpack &args)
{
    (void)args;
    flushed_ = true;
}

} // namespace rv_editor
