// Drawing nvim grids and floating windows overlays.

#include "nvim/rv_editor_nvim_grid.hpp"

#include <algorithm>
#include <cmath>
#include <string>

#include "imgui.h"

namespace rv_editor
{

ImU32 rv_editor_rgb(uint32_t rgb)
{
    return IM_COL32((rgb >> 16) & 0xff, (rgb >> 8) & 0xff, rgb & 0xff, 255);
}

void rv_editor_nvim_draw_grid(const rv_editor_nvim_screen &screen, const rv_editor_nvim_grid &grid, ImVec2 at,
    ImVec2 cell, int32_t rows, bool cursor)
{
    ImDrawList *dl = ImGui::GetWindowDrawList();
    std::string run;
    for (int32_t r = 0; r < std::min(rows, grid.height); ++r) {
        int32_t c = 0;
        while (c < grid.width) {
            const int32_t hl = grid.cells[static_cast<size_t>(r * grid.width + c)].hl;
            const int32_t start = c;
            run.clear();
            while (c < grid.width && grid.cells[static_cast<size_t>(r * grid.width + c)].hl == hl) {
                const std::string &t = grid.cells[static_cast<size_t>(r * grid.width + c)].text;
                run += t; // a double-width character's second cell is empty
                ++c;
            }
            uint32_t fg = 0;
            uint32_t bg = 0;
            screen.colors(hl, fg, bg);
            const ImVec2 p0(at.x + start * cell.x, at.y + r * cell.y);
            dl->AddRectFilled(p0, ImVec2(at.x + c * cell.x, p0.y + cell.y), rv_editor_rgb(bg));
            dl->AddText(p0, rv_editor_rgb(fg), run.c_str());
            if (screen.attr(hl).underline) {
                dl->AddLine(ImVec2(p0.x, p0.y + cell.y - 1), ImVec2(at.x + c * cell.x, p0.y + cell.y - 1),
                    rv_editor_rgb(fg));
            }
        }
    }
    if (!cursor || grid.cursor_row < 0 || grid.cursor_row >= std::min(rows, grid.height) || grid.cursor_col < 0 ||
        grid.cursor_col >= grid.width) {
        return;
    }
    const rv_editor_nvim_cell &under = grid.cells[static_cast<size_t>(grid.cursor_row * grid.width + grid.cursor_col)];
    uint32_t fg = 0;
    uint32_t bg = 0;
    screen.colors(under.hl, fg, bg);
    const ImVec2 p0(at.x + grid.cursor_col * cell.x, at.y + grid.cursor_row * cell.y);
    // The shape nvim gives the current mode (guicursor, mode_info_set).
    const rv_editor_nvim_cursor shape = screen.cursor_shape();
    if (shape.kind == rv_editor_nvim_cursor_kind::vertical) {
        const float w = std::max(2.0f, std::floor(cell.x * static_cast<float>(shape.percent) / 100.0f));
        dl->AddRectFilled(p0, ImVec2(p0.x + w, p0.y + cell.y), rv_editor_rgb(fg));
        return;
    }
    if (shape.kind == rv_editor_nvim_cursor_kind::horizontal) {
        const float h = std::max(2.0f, std::floor(cell.y * static_cast<float>(shape.percent) / 100.0f));
        dl->AddRectFilled(ImVec2(p0.x, p0.y + cell.y - h), ImVec2(p0.x + cell.x, p0.y + cell.y), rv_editor_rgb(fg));
        return;
    }
    dl->AddRectFilled(p0, ImVec2(p0.x + cell.x, p0.y + cell.y), rv_editor_rgb(fg));
    dl->AddText(p0, rv_editor_rgb(bg), under.text.c_str());
}

// Floating grids sorted by zindex, shifted and clipped to tile bounds.
void rv_editor_nvim_draw_floats(const rv_editor_nvim_screen &screen, int32_t grid_id, ImVec2 tile_at, ImVec2 cell,
    int32_t tile_cols, int32_t tile_rows, bool tile_focused)
{
    ImDrawList *dl = ImGui::GetWindowDrawList();
    const ImVec2 tile_bounds(tile_at.x + tile_cols * cell.x, tile_at.y + tile_rows * cell.y);

    for (const auto &fg : screen.float_grids()) {
        const rv_editor_nvim_grid *grid = fg.grid;

        if ((grid->anchor_grid != grid_id) && !(grid->anchor_grid == 1 && tile_focused)) {
            continue;
        }

        double float_row = grid->anchor_row;
        double float_col = grid->anchor_col;

        // Anchor relative to grid 1: translate to tile coordinates using tile window position.
        if (grid->anchor_grid == 1) {
            const rv_editor_nvim_grid *tile_grid = screen.grid(grid_id);
            if (tile_grid != nullptr) {
                float_row -= tile_grid->window_row;
                float_col -= tile_grid->window_col;
            }
        }

        if (grid->anchor == "NE") {
            float_col -= grid->width;
        } else if (grid->anchor == "SW") {
            float_row -= grid->height;
        } else if (grid->anchor == "SE") {
            float_row -= grid->height;
            float_col -= grid->width;
        }

        ImVec2 float_at(tile_at.x + float_col * cell.x, tile_at.y + float_row * cell.y);

        const ImVec2 float_bounds(float_at.x + grid->width * cell.x, float_at.y + grid->height * cell.y);
        if (float_at.x < tile_at.x) {
            float_at.x = tile_at.x;
        }
        if (float_at.y < tile_at.y) {
            float_at.y = tile_at.y;
        }
        if (float_bounds.x > tile_bounds.x) {
            float_at.x = std::max(tile_at.x, tile_bounds.x - grid->width * cell.x);
        }
        if (float_bounds.y > tile_bounds.y) {
            float_at.y = std::max(tile_at.y, tile_bounds.y - grid->height * cell.y);
        }

        dl->PushClipRect(tile_at, tile_bounds, true);
        const bool cursor_here = tile_focused && screen.cursor_grid() == fg.id;
        rv_editor_nvim_draw_grid(screen, *grid, float_at, cell, grid->height, cursor_here);
        dl->PopClipRect();
    }
}

} // namespace rv_editor
