// nvim's linegrid/multigrid redraw events applied to an in-memory screen.

#include "nvim/rv_editor_nvim_grid.hpp"

#include <algorithm>

namespace rv_editor
{

namespace
{

using mtype = rv_editor_mpack::rv_editor_mpack_type;

// Redraw event types for nvim's ui protocol.
enum class redraw_event_type {
    grid_line,
    grid_resize,
    grid_clear,
    grid_scroll,
    grid_cursor_goto,
    grid_destroy,
    win_float_pos,
    win_pos,
    win_hide,
    win_close,
    msg_set_pos,
    hl_attr_define,
    default_colors_set,
    mode_info_set,
    mode_change,
    flush,
    unknown,
};

// nvim's linegrid redraw events (ui protocol) this client handles.
constexpr std::pair<std::string_view, redraw_event_type> event_name_map[] = {
    { "grid_line", redraw_event_type::grid_line },
    { "grid_resize", redraw_event_type::grid_resize },
    { "grid_clear", redraw_event_type::grid_clear },
    { "grid_scroll", redraw_event_type::grid_scroll },
    { "grid_cursor_goto", redraw_event_type::grid_cursor_goto },
    { "grid_destroy", redraw_event_type::grid_destroy },
    { "win_float_pos", redraw_event_type::win_float_pos },
    { "win_pos", redraw_event_type::win_pos },
    { "win_hide", redraw_event_type::win_hide },
    { "win_close", redraw_event_type::win_close },
    { "msg_set_pos", redraw_event_type::msg_set_pos },
    { "hl_attr_define", redraw_event_type::hl_attr_define },
    { "default_colors_set", redraw_event_type::default_colors_set },
    { "mode_info_set", redraw_event_type::mode_info_set },
    { "mode_change", redraw_event_type::mode_change },
    { "flush", redraw_event_type::flush },
};

redraw_event_type event_type_from_name(std::string_view name)
{
    for (const auto &[event_name, event_type] : event_name_map) {
        if (name == event_name) {
            return event_type;
        }
    }
    return redraw_event_type::unknown;
}

// Minimum argument counts for nvim protocol events
constexpr size_t grid_line_args = 4;
constexpr size_t grid_scroll_args = 6;
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

// Default cell content for empty grid cells
constexpr const char *empty_cell_text = " ";

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
    std::vector<rv_editor_nvim_cell> cells(static_cast<size_t>(std::max(0, w) * std::max(0, h)), { empty_cell_text, 0 });
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
    switch (event_type_from_name(name)) {
    case redraw_event_type::grid_line:
        grid_line(args);
        break;
    case redraw_event_type::grid_resize:
        on_grid_resize(args);
        break;
    case redraw_event_type::grid_clear:
        on_grid_clear(args);
        break;
    case redraw_event_type::grid_scroll:
        grid_scroll(args);
        break;
    case redraw_event_type::grid_cursor_goto:
        on_grid_cursor_goto(args);
        break;
    case redraw_event_type::grid_destroy:
        on_grid_destroy(args);
        break;
    case redraw_event_type::win_float_pos:
        on_win_float_pos(args);
        break;
    case redraw_event_type::win_pos:
        on_win_pos(args);
        break;
    case redraw_event_type::win_hide:
    case redraw_event_type::win_close:
        on_win_hidden(args);
        break;
    case redraw_event_type::msg_set_pos:
        on_msg_set_pos(args);
        break;
    case redraw_event_type::hl_attr_define:
        on_hl_attr_define(args);
        break;
    case redraw_event_type::default_colors_set:
        on_default_colors_set(args);
        break;
    case redraw_event_type::mode_info_set:
        on_mode_info_set(args);
        break;
    case redraw_event_type::mode_change:
        on_mode_change(args);
        break;
    case redraw_event_type::flush:
        on_flush(args);
        break;
    case redraw_event_type::unknown:
        // nvim sends events this client does not render
        break;
    }
}

} // namespace rv_editor
