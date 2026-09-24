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

} // namespace

const rv_editor_nvim_grid *rv_editor_nvim_screen::grid(int32_t id) const
{
    const auto it = grids_.find(id);
    return it == grids_.end() ? nullptr : &it->second;
}

int32_t rv_editor_nvim_screen::grid_of_window(int64_t win) const
{
    for (const auto &[id, g] : grids_) {
        if (g.win == win && !g.hidden) {
            return id;
        }
    }
    return 0;
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

void rv_editor_nvim_screen::grid_resize(int32_t id, int32_t w, int32_t h)
{
    rv_editor_nvim_grid &g = grids_[id];
    std::vector<rv_editor_nvim_cell> cells(static_cast<size_t>(std::max(0, w) * std::max(0, h)), { " ", 0 });
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
    if (args.items.size() < 4) {
        return;
    }
    const auto it = grids_.find(rv_editor_int(args.items[0]));
    if (it == grids_.end()) {
        return;
    }
    rv_editor_nvim_grid &g = it->second;
    const int32_t row = rv_editor_int(args.items[1]);
    int32_t col = rv_editor_int(args.items[2]);
    if (row < 0 || row >= g.height || col < 0) {
        return;
    }
    int32_t hl = 0;
    for (const rv_editor_mpack &cell : args.items[3].items) {
        if (cell.items.empty()) {
            continue;
        }
        // A cell without hl_id repeats the previous one's.
        if (cell.items.size() >= 2) {
            hl = rv_editor_int(cell.items[1]);
        }
        const int32_t repeat = cell.items.size() >= 3 ? rv_editor_int(cell.items[2]) : 1;
        for (int32_t n = 0; n < repeat && col < g.width; ++n, ++col) {
            g.at(row, col) = { cell.items[0].s, hl };
        }
    }
}

void rv_editor_nvim_screen::grid_scroll(const rv_editor_mpack &args)
{
    // grid_scroll(grid, top, bot, left, right, rows, cols): rows > 0 moves text up.
    if (args.items.size() < 6) {
        return;
    }
    const auto it = grids_.find(rv_editor_int(args.items[0]));
    if (it == grids_.end()) {
        return;
    }
    rv_editor_nvim_grid &g = it->second;
    const int32_t top = std::clamp(rv_editor_int(args.items[1]), 0, g.height);
    const int32_t bot = std::clamp(rv_editor_int(args.items[2]), 0, g.height);
    const int32_t left = std::clamp(rv_editor_int(args.items[3]), 0, g.width);
    const int32_t right = std::clamp(rv_editor_int(args.items[4]), 0, g.width);
    const int32_t rows = rv_editor_int(args.items[5]);
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
    if (name == "grid_line") {
        grid_line(args);
    } else if (name == "grid_resize" && a.size() >= 3) {
        grid_resize(rv_editor_int(a[0]), rv_editor_int(a[1]), rv_editor_int(a[2]));
    } else if (name == "grid_clear" && !a.empty()) {
        const auto it = grids_.find(rv_editor_int(a[0]));
        if (it != grids_.end()) {
            std::fill(it->second.cells.begin(), it->second.cells.end(), rv_editor_nvim_cell{ " ", 0 });
        }
    } else if (name == "grid_scroll") {
        grid_scroll(args);
    } else if (name == "grid_cursor_goto" && a.size() >= 3) {
        cursor_grid_ = rv_editor_int(a[0]);
        rv_editor_nvim_grid &g = grids_[cursor_grid_];
        g.cursor_row = rv_editor_int(a[1]);
        g.cursor_col = rv_editor_int(a[2]);
    } else if (name == "grid_destroy" && !a.empty()) {
        grids_.erase(rv_editor_int(a[0]));
    } else if (name == "win_pos" && a.size() >= 2) {
        rv_editor_nvim_grid &g = grids_[rv_editor_int(a[0])];
        g.win = a[1].i;
        g.hidden = false;
    } else if ((name == "win_hide" || name == "win_close") && !a.empty()) {
        const auto it = grids_.find(rv_editor_int(a[0]));
        if (it != grids_.end()) {
            it->second.hidden = true;
        }
    } else if (name == "msg_set_pos" && a.size() >= 2) {
        msg_grid_ = rv_editor_int(a[0]);
        msg_row_ = rv_editor_int(a[1]);
    } else if (name == "hl_attr_define" && a.size() >= 2) {
        rv_editor_nvim_attr attr;
        const rv_editor_mpack &rgb = a[1];
        if (const rv_editor_mpack *v = rgb.get("foreground")) {
            attr.fg = static_cast<uint32_t>(v->i);
            attr.has_fg = true;
        }
        if (const rv_editor_mpack *v = rgb.get("background")) {
            attr.bg = static_cast<uint32_t>(v->i);
            attr.has_bg = true;
        }
        attr.reverse = rgb.get("reverse") != nullptr && rgb.get("reverse")->b;
        attr.bold = rgb.get("bold") != nullptr && rgb.get("bold")->b;
        attr.italic = rgb.get("italic") != nullptr && rgb.get("italic")->b;
        attr.underline = (rgb.get("underline") != nullptr && rgb.get("underline")->b) ||
            (rgb.get("undercurl") != nullptr && rgb.get("undercurl")->b);
        attrs_[rv_editor_int(a[0])] = attr;
    } else if (name == "default_colors_set" && a.size() >= 2) {
        default_.fg = static_cast<uint32_t>(a[0].i);
        default_.bg = static_cast<uint32_t>(a[1].i);
        default_.has_fg = default_.has_bg = true;
    } else if (name == "mode_change" && !a.empty()) {
        mode_ = a[0].s;
    } else if (name == "flush") {
        flushed_ = true;
    }
}

} // namespace rv_editor
