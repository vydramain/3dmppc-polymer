// The editor window's least size: the 1280x720 layout minimum, the width more when the status bar needs it.

#include <algorithm>
#include <cmath>

#include "app/rv_editor_shell.hpp"

namespace rv_editor
{

bool rv_editor_window_min_sync(SDL_Window *window, rv_editor_shell &shell, const rv_editor_theme &theme)
{
    int min_w = 0;
    if (!rv_editor_shell_window_min_changed(shell, theme, min_w)) {
        return true;
    }
    const float density = SDL_GetWindowPixelDensity(window);
    const int w_points = static_cast<int>(std::ceil(static_cast<float>(min_w) / density));
    const int h_points = static_cast<int>(std::ceil(window_min_height * theme.scale / density));
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
