// nvim's linegrid/multigrid redraw events applied to an in-memory screen.

#include "nvim/rv_editor_nvim_grid.hpp"

#include <algorithm>

namespace rv_editor
{

namespace
{

using mtype = rv_editor_mpack::rv_editor_mpack_type;

int32_t rv_editor_int(const rv_editor_mpack &v)
{
    return v.is(mtype::integer) || v.is(mtype::ext) ? static_cast<int32_t>(v.i) : 0;
}

double rv_editor_real_or_int(const rv_editor_mpack &v)
{
    return v.is(mtype::real) ? v.d : static_cast<double>(rv_editor_int(v));
}

// Nvim linegrid redraw event names from ui protocol
constexpr std::string_view event_grid_line = "grid_line";
constexpr std::string_view event_grid_resize = "grid_resize";
constexpr std::string_view event_grid_clear = "grid_clear";
constexpr std::string_view event_grid_scroll = "grid_scroll";
constexpr std::string_view event_grid_cursor_goto = "grid_cursor_goto";
constexpr std::string_view event_grid_destroy = "grid_destroy";
constexpr std::string_view event_win_float_pos = "win_float_pos";
constexpr std::string_view event_win_pos = "win_pos";
constexpr std::string_view event_win_hide = "win_hide";
constexpr std::string_view event_win_close = "win_close";
constexpr std::string_view event_msg_set_pos = "msg_set_pos";
constexpr std::string_view event_hl_attr_define = "hl_attr_define";
constexpr std::string_view event_default_colors_set = "default_colors_set";
constexpr std::string_view event_mode_info_set = "mode_info_set";
constexpr std::string_view event_mode_change = "mode_change";
constexpr std::string_view event_flush = "flush";

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

// Minimum argument counts for nvim protocol events
constexpr size_t grid_line_args = 4;
constexpr size_t grid_scroll_args = 6;
constexpr size_t grid_resize_args = 3;
constexpr size_t grid_cursor_goto_args = 3;
constexpr size_t win_float_pos_args = 7;
constexpr size_t win_float_zindex_args = 8;
constexpr size_t win_pos_args = 4;
constexpr size_t msg_set_pos_args = 2;
constexpr size_t hl_attr_define_args = 2;
constexpr size_t mode_info_set_args = 2;
constexpr size_t mode_change_args = 2;
constexpr size_t cell_hl_id_args = 2;
constexpr size_t cell_repeat_args = 3;

// grid_line event argument positions
constexpr size_t grid_line_grid = 0;
constexpr size_t grid_line_row = 1;
constexpr size_t grid_line_col_start = 2;
constexpr size_t grid_line_cells = 3;

// grid_line cell array element positions
constexpr size_t cell_text = 0;
constexpr size_t cell_hl_id = 1;
constexpr size_t cell_repeat = 2;

// grid_scroll event argument positions
constexpr size_t grid_scroll_grid = 0;
constexpr size_t grid_scroll_top = 1;
constexpr size_t grid_scroll_bot = 2;
constexpr size_t grid_scroll_left = 3;
constexpr size_t grid_scroll_right = 4;
constexpr size_t grid_scroll_rows = 5;

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

// Protocol-defined constants
constexpr int32_t default_float_zindex = 50;
constexpr int32_t cell_percentage_max = 100;
constexpr const char *empty_cell_text = " "; // Default cell content for empty grid cells

} // namespace

const rv_editor_nvim_grid *rv_editor_nvim_screen::grid(int32_t id) const
{
    const auto it = grids_.find(id);
    return it == grids_.end() ? nullptr : &it->second;
}

int32_t rv_editor_nvim_screen::grid_of_window(int64_t win) const
{
    for (const auto &[id, g] : grids_) {
        if (g.win == win && !g.hidden && !g.is_float) {
            return id;
        }
    }
    return 0;
}

std::vector<rv_editor_nvim_screen::float_grid> rv_editor_nvim_screen::float_grids() const
{
    std::vector<float_grid> result;
    for (const auto &[id, g] : grids_) {
        if (g.is_float && !g.hidden) {
            result.push_back({ id, &g });
        }
    }
    // Sort by zindex ascending.
    std::sort(result.begin(), result.end(), [](const float_grid &a, const float_grid &b) {
        return a.grid->zindex < b.grid->zindex;
    });
    return result;
}

const rv_editor_nvim_attr &rv_editor_nvim_screen::attr(int32_t hl) const
{
    const auto it = attrs_.find(hl);
    return it == attrs_.end() ? default_ : it->second;
}

void rv_editor_nvim_screen::colors(int32_t hl, uint32_t &fg, uint32_t &bg) const
{
    const rv_editor_nvim_attr &a = attr(hl);
    fg = a.has_fg ? a.fg : default_.fg;
    bg = a.has_bg ? a.bg : default_.bg;
    if (a.reverse) {
        std::swap(fg, bg);
    }
}

void rv_editor_nvim_screen::apply(const rv_editor_mpack &batches)
{
    flushed_ = false;
    for (const rv_editor_mpack &batch : batches.items) {
        if (!batch.is(mtype::array) || batch.items.empty() || !batch.items[0].is(mtype::string)) {
            continue;
        }
        const std::string &name = batch.items[0].s;
        // Each further item is one call's argument list.
        for (size_t k = 1; k < batch.items.size(); ++k) {
            event(name, batch.items[k]);
        }
    }
}

rv_editor_nvim_cursor rv_editor_nvim_screen::cursor_shape() const
{
    if (mode_index_ < 0 || static_cast<size_t>(mode_index_) >= mode_info_.size()) {
        return {};
    }
    return mode_info_[static_cast<size_t>(mode_index_)];
}

void rv_editor_nvim_screen::grid_resize(int32_t id, int32_t w, int32_t h)
{
    rv_editor_nvim_grid &g = grids_[id];
    std::vector<rv_editor_nvim_cell> cells(static_cast<size_t>(std::max(0, w) * std::max(0, h)),
        { empty_cell_text, 0 });
    for (int32_t r = 0; r < std::min(h, g.height); ++r) {
        for (int32_t c = 0; c < std::min(w, g.width); ++c) {
            cells[static_cast<size_t>(r * w + c)] = g.at(r, c);
        }
    }
    g.width = std::max(0, w);
    g.height = std::max(0, h);
    g.cells = std::move(cells);
}

void rv_editor_nvim_screen::grid_line(const rv_editor_mpack &args)
{
    // grid_line(grid, row, col_start, [[text, hl_id?, repeat?], ...], wrap)
    if (args.items.size() < grid_line_args) {
        return;
    }
    const auto it = grids_.find(rv_editor_int(args.items[grid_line_grid]));
    if (it == grids_.end()) {
        return;
    }
    rv_editor_nvim_grid &g = it->second;
    const int32_t row = rv_editor_int(args.items[grid_line_row]);
    int32_t col = rv_editor_int(args.items[grid_line_col_start]);
    if (row < 0 || row >= g.height || col < 0) {
        return;
    }
    int32_t hl = 0;
    for (const rv_editor_mpack &cell : args.items[grid_line_cells].items) {
        if (cell.items.empty()) {
            continue;
        }
        // A cell without hl_id repeats the previous one's.
        if (cell.items.size() >= cell_hl_id_args) {
            hl = rv_editor_int(cell.items[cell_hl_id]);
        }
        const int32_t repeat = cell.items.size() >= cell_repeat_args ? rv_editor_int(cell.items[cell_repeat]) : 1;
        for (int32_t n = 0; n < repeat && col < g.width; ++n, ++col) {
            g.at(row, col) = { cell.items[cell_text].s, hl };
        }
    }
}

void rv_editor_nvim_screen::grid_scroll(const rv_editor_mpack &args)
{
    // grid_scroll(grid, top, bot, left, right, rows, cols): rows > 0 moves text up.
    if (args.items.size() < grid_scroll_args) {
        return;
    }
    const auto it = grids_.find(rv_editor_int(args.items[grid_scroll_grid]));
    if (it == grids_.end()) {
        return;
    }
    rv_editor_nvim_grid &g = it->second;
    const int32_t top = std::clamp(rv_editor_int(args.items[grid_scroll_top]), 0, g.height);
    const int32_t bot = std::clamp(rv_editor_int(args.items[grid_scroll_bot]), 0, g.height);
    const int32_t left = std::clamp(rv_editor_int(args.items[grid_scroll_left]), 0, g.width);
    const int32_t right = std::clamp(rv_editor_int(args.items[grid_scroll_right]), 0, g.width);
    const int32_t rows = rv_editor_int(args.items[grid_scroll_rows]);
    if (rows > 0) {
        for (int32_t r = top; r + rows < bot; ++r) {
            for (int32_t c = left; c < right; ++c) {
                g.at(r, c) = g.at(r + rows, c);
            }
        }
    } else if (rows < 0) {
        for (int32_t r = bot - 1; r + rows >= top; --r) {
            for (int32_t c = left; c < right; ++c) {
                g.at(r, c) = g.at(r + rows, c);
            }
        }
    }
}

void rv_editor_nvim_screen::event(const std::string &name, const rv_editor_mpack &args)
{
    const auto &a = args.items;
    if (name == event_grid_line) {
        grid_line(args);
    } else if (name == event_grid_resize && a.size() >= grid_resize_args) {
        grid_resize(rv_editor_int(a[grid_resize_grid]), rv_editor_int(a[grid_resize_width]),
            rv_editor_int(a[grid_resize_height]));
    } else if (name == event_grid_clear && !a.empty()) {
        const auto it = grids_.find(rv_editor_int(a[grid_line_grid]));
        if (it != grids_.end()) {
            std::fill(it->second.cells.begin(), it->second.cells.end(),
                rv_editor_nvim_cell{ empty_cell_text, 0 });
        }
    } else if (name == event_grid_scroll) {
        grid_scroll(args);
    } else if (name == event_grid_cursor_goto && a.size() >= grid_cursor_goto_args) {
        cursor_grid_ = rv_editor_int(a[grid_cursor_goto_grid]);
        rv_editor_nvim_grid &g = grids_[cursor_grid_];
        g.cursor_row = rv_editor_int(a[grid_cursor_goto_row]);
        g.cursor_col = rv_editor_int(a[grid_cursor_goto_col]);
    } else if (name == event_grid_destroy && !a.empty()) {
        grids_.erase(rv_editor_int(a[grid_line_grid]));
    } else if (name == event_win_float_pos && a.size() >= win_float_pos_args) {
        rv_editor_nvim_grid &g = grids_[rv_editor_int(a[win_float_pos_grid])];
        g.win = rv_editor_int(a[win_float_pos_win]);
        g.is_float = true;
        g.anchor = a[win_float_pos_anchor].s;
        g.anchor_grid = rv_editor_int(a[win_float_pos_anchor_grid]);
        // nvim sends row/col as floats; store as-is for rendering precision.
        g.anchor_row = rv_editor_real_or_int(a[win_float_pos_anchor_row]);
        g.anchor_col = rv_editor_real_or_int(a[win_float_pos_anchor_col]);
        g.zindex = a.size() >= win_float_zindex_args ? rv_editor_int(a[win_float_pos_zindex]) :
                                                       default_float_zindex;
        g.hidden = false;
    } else if (name == event_win_pos && a.size() >= win_pos_args) {
        rv_editor_nvim_grid &g = grids_[rv_editor_int(a[win_pos_grid])];
        g.win = a[win_pos_win].i;
        g.is_float = false;
        g.hidden = false;
        g.window_row = rv_editor_real_or_int(a[win_pos_start_row]);
        g.window_col = rv_editor_real_or_int(a[win_pos_start_col]);
    } else if ((name == event_win_hide || name == event_win_close) && !a.empty()) {
        const auto it = grids_.find(rv_editor_int(a[grid_line_grid]));
        if (it != grids_.end()) {
            it->second.hidden = true;
        }
    } else if (name == event_msg_set_pos && a.size() >= msg_set_pos_args) {
        msg_grid_ = rv_editor_int(a[grid_line_grid]);
        msg_row_ = rv_editor_int(a[grid_line_row]);
    } else if (name == event_hl_attr_define && a.size() >= hl_attr_define_args) {
        rv_editor_nvim_attr attr;
        const rv_editor_mpack &rgb = a[1];
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
        attrs_[rv_editor_int(a[0])] = attr;
    } else if (name == event_default_colors_set && a.size() >= hl_attr_define_args) {
        default_.fg = static_cast<uint32_t>(a[0].i);
        default_.bg = static_cast<uint32_t>(a[1].i);
        default_.has_fg = default_.has_bg = true;
    } else if (name == event_mode_info_set && a.size() >= mode_info_set_args) {
        mode_info_.clear();
        for (const rv_editor_mpack &m : a[1].items) {
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
    } else if (name == event_mode_change && !a.empty()) {
        mode_ = a[0].s;
        mode_index_ = a.size() >= mode_change_args ? rv_editor_int(a[1]) : -1;
    } else if (name == event_flush) {
        flushed_ = true;
    }
}

} // namespace rv_editor
