// The editor window's least size: the scaled layout minimum, with room for the status bar up to the display width.

#include <algorithm>
#include <cmath>

#include "app/rv_editor_shell.hpp"

namespace rv_editor
{

bool rv_editor_window_min_sync(SDL_Window *window, rv_editor_shell &shell, const rv_editor_theme &theme)
{
    int min_w = 0;
    const bool width_changed = rv_editor_shell_window_min_changed(shell, theme, min_w);
    const int display_w = rv_editor_get_display_size(window).w_pixels;
    if (display_w > 0) {
        min_w = std::min(min_w, display_w);
    }
    const float density = SDL_GetWindowPixelDensity(window);
    const int w_points = static_cast<int>(std::ceil(static_cast<float>(min_w) / density));
    const int h_points = static_cast<int>(std::ceil(window_min_height * theme.scale / density));
    if (!width_changed) {
        int old_w = 0;
        int old_h = 0;
        if (!SDL_GetWindowMinimumSize(window, &old_w, &old_h)) {
            return false;
        }
        if (old_w == w_points && old_h == h_points) {
            return true;
        }
    }
    if (!SDL_SetWindowMinimumSize(window, w_points, h_points)) {
        return false;
    }
    int cur_w = 0;
    int cur_h = 0;
    SDL_GetWindowSize(window, &cur_w, &cur_h);
    if (cur_w < w_points || cur_h < h_points) {
        return SDL_SetWindowSize(window, std::max(cur_w, w_points), std::max(cur_h, h_points));
    }
    return true;
}

} // namespace rv_editor
