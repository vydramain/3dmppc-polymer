// 3dmppc-editor entry point: one SDL3 window and one Dear ImGui frame loop.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <getopt.h>
#include <memory>
#include <string>

#include <SDL3/SDL.h>

#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_sdlrenderer3.h"

#include "pdk/rv_err.h"

#include "app/rv_editor_shell.hpp"
#include "font/rv_editor_font.hpp"
#include "panes/rv_editor_panes.hpp"
#include "platform/rv_editor_process.hpp"
#include "prefs/rv_editor_prefs.hpp"
#include "theme/rv_editor_theme.hpp"
#include "theme/rv_editor_theme_imgui.hpp"
#include "ui/rv_editor_icons.hpp"
#include "ui/rv_editor_sound.hpp"
#include "ui/rv_editor_widgets.hpp"

namespace
{

void rv_editor_usage(std::FILE *out)
{
    std::fprintf(out,
        "usage: 3dmppc-editor [PATH]\n"
        "  PATH            A game directory, or its disc.toml, to open.\n");
}

// Path from the command line. Returns RV_OK for successful parse, RV_ERR_INVAL for errors.
// Sets exit_code only when caller should exit: 0 for --help, 2 for errors.
int rv_editor_args_parse(int argc, char **argv, std::string &path, int &exit_code)
{
    static struct option long_opts[] = { { "help", no_argument, 0, 'h' }, { 0, 0, 0, 0 } };

    int c;
    while ((c = getopt_long(argc, argv, "h", long_opts, nullptr)) != -1) {
        switch (c) {
        case 'h':
            rv_editor_usage(stdout);
            exit_code = 0;
            return RV_OK;
        case '?':
            // getopt has already printed its own error message on stderr.
            rv_editor_usage(stderr);
            exit_code = 2;
            return RV_ERR_INVAL;
        }
    }

    // optind is where getopt_long left the first non-flag argument.
    if (argc - optind > 1) {
        std::fprintf(stderr, "3dmppc-editor: unexpected argument '%s'\n", argv[optind + 1]);
        rv_editor_usage(stderr);
        exit_code = 2;
        return RV_ERR_INVAL;
    }

    if (argc - optind == 1) {
        path = argv[optind];
    }

    return RV_OK;
}

// A fixed strip of the main window: the status bar or the tiles' host. End() it
// whatever this returns, as with ImGui::Begin.
bool rv_editor_bar_begin(const char *id, ImVec2 pos, ImVec2 size)
{
    ImGui::SetNextWindowPos(pos);
    ImGui::SetNextWindowSize(size);
    constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBringToFrontOnFocus;
    return ImGui::Begin(id, nullptr, flags);
}

// One frame of the editor's UI: menus, the tiles and the status bar.
void rv_editor_frame(rv_editor::rv_editor_shell &shell, const rv_editor::rv_editor_theme &theme)
{
    // The background list is rendered first, and the backend resets sampling only
    // at the start of a render: one request here keeps the whole frame unsmoothed.
    ImGui::GetBackgroundDrawList()->AddCallback(ImGui::GetPlatformIO().DrawCallback_SetSamplerNearest, nullptr);
    rv_editor::rv_editor_shell_menu(shell);
    rv_editor::rv_editor_shell_shortcuts(shell);

    // The status bar takes a row; the tiles get the rest.
    const ImGuiViewport *viewport = ImGui::GetMainViewport();
    const ImVec2 top = viewport->WorkPos;
    const ImVec2 size = viewport->WorkSize;
    const float bar = ImGui::GetFrameHeight() + 2.0f * ImGui::GetStyle().WindowPadding.y;
    if (rv_editor_bar_begin("##status", ImVec2(top.x, top.y + size.y - bar), ImVec2(size.x, bar))) {
        rv_editor::rv_editor_shell_status(shell, theme);
    }
    ImGui::End();

    const rv_editor::rv_editor_rect area{ static_cast<int>(top.x), static_cast<int>(top.y),
        static_cast<int>(size.x), static_cast<int>(size.y - bar) };
    // The tiles get a host strip of their own, like the bars around them.
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    const bool host = rv_editor_bar_begin("##tiles", top, ImVec2(size.x, size.y - bar));
    ImGui::PopStyleVar();
    // No project: its start screen instead of a grid of empty tiles; the
    // layouts wait unchanged.
    if (host && !shell.app.project.open) {
        rv_editor::rv_editor_shell_start_screen(shell, theme);
    } else if (host) {
        rv_editor::rv_editor_workspace_draw(shell.ws, theme, rv_editor::rv_editor_shell_pane,
            rv_editor::rv_editor_shell_close_pane, &shell, area);
    }
    ImGui::End();
    rv_editor::rv_editor_shell_game_input(shell);
}

// True once the user asked the window to close. Pointer coordinates are turned
// into render pixels first: ImGui works in pixels, not in the desktop's points.
// `focused` follows the window's keyboard focus.
bool rv_editor_poll(SDL_Window *window, SDL_Renderer *renderer, bool &focused)
{
    bool quit = false;
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        SDL_ConvertEventToRenderCoordinates(renderer, &event);
        ImGui_ImplSDL3_ProcessEvent(&event);
        if (event.type == SDL_EVENT_QUIT) {
            quit = true;
        }
        if (event.type == SDL_EVENT_WINDOW_FOCUS_LOST || event.type == SDL_EVENT_WINDOW_FOCUS_GAINED) {
            focused = event.type == SDL_EVENT_WINDOW_FOCUS_GAINED;
        }
        if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED && event.window.windowID == SDL_GetWindowID(window)) {
            quit = true;
        }
    }
    return quit;
}

// The backend reports the window in points and asks the renderer to scale by the
// display's density. The editor takes the window in pixels at density 1 instead,
// so no scale is applied beyond what the View menu and saved settings choose.
void rv_editor_display_pixels(SDL_Window *window)
{
    int w = 0;
    int h = 0;
    SDL_GetWindowSizeInPixels(window, &w, &h);
    ImGuiIO &io = ImGui::GetIO();
    io.DisplaySize = ImVec2(static_cast<float>(w), static_cast<float>(h));
    io.DisplayFramebufferScale = ImVec2(1.0f, 1.0f);
}

// Fonts for a scale replace the current ones only when all are built; the style takes the interface size.
int rv_editor_fonts_build(ImGuiIO &io, float scale)
{
    if (!rv_editor::rv_editor_fonts_add(*io.Fonts, scale)) {
        return RV_ERR_NOENT;
    }
    ImGui::GetStyle().FontSizeBase = rv_editor::rv_editor_font_ui()->LegacySize;
    return RV_OK;
}

// Set minimum window size to 1280x720 UI points at the given scale, accounting for pixel density (window can be larger).
// If resize_up, enlarge to minimum when too small; otherwise leave size unchanged.
// Window must be valid (post-creation).
int rv_editor_set_window_size_for_scale(SDL_Window *window, float scale, bool resize_up)
{
    const float density = SDL_GetWindowPixelDensity(window);
    const int w_points = static_cast<int>(std::ceil(1280 * scale / density));
    const int h_points = static_cast<int>(std::ceil(720 * scale / density));
    if (!SDL_SetWindowMinimumSize(window, w_points, h_points)) {
        return RV_ERR_IO;
    }
    if (resize_up) {
        int cur_w;
        int cur_h;
        SDL_GetWindowSize(window, &cur_w, &cur_h);
        if (cur_w < w_points || cur_h < h_points) {
            if (!SDL_SetWindowSize(window, std::max(cur_w, w_points), std::max(cur_h, h_points))) {
                return RV_ERR_IO;
            }
        }
    }
    return RV_OK;
}

} // namespace

int main(int argc, char **argv)
{
    std::string open_path;
    int exit_code = -1;
    if (rv_editor_args_parse(argc, argv, open_path, exit_code) != RV_OK) {
        return exit_code >= 0 ? exit_code : 2;
    }
    if (exit_code >= 0) {
        return exit_code;
    }

    rv_editor::rv_editor_guard_start();

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::fprintf(stderr, "3dmppc-editor: SDL_Init: %s\n", SDL_GetError());
        return 1;
    }

    SDL_Window *window = nullptr;
    SDL_Renderer *renderer = nullptr;
    constexpr SDL_WindowFlags window_flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;
    if (!SDL_CreateWindowAndRenderer("3dmppc-editor", 1280, 720, window_flags, &window, &renderer)) {
        std::fprintf(stderr, "3dmppc-editor: SDL_CreateWindowAndRenderer: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    if (!SDL_SetRenderVSync(renderer, 1)) {
        std::fprintf(stderr, "3dmppc-editor: SDL_SetRenderVSync: %s\n", SDL_GetError());
    }

    // Check if display is large enough (needs 1280x720 at scale 1.0).
    const rv_editor::rv_editor_display_size display_size = rv_editor::rv_editor_get_display_size(window);
    if (display_size.w_pixels < 1280 || display_size.h_pixels < 720) {
        rv_editor::rv_editor_show_too_small_error(window, renderer, display_size);
        SDL_Quit();
        return 1;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    // The editor will keep its own layout file; ImGui writes none.
    io.IniFilename = nullptr;

    // The view the user chose last time: code text size, Game scale, layout, UI scale.
    const std::filesystem::path prefs_path = rv_editor::rv_editor_prefs_file_path();
    const rv_editor::rv_editor_prefs prefs = rv_editor::rv_editor_prefs_load(prefs_path);
    rv_editor::rv_editor_theme theme = rv_editor::rv_editor_theme_olive;
    theme.scale = prefs.ui_scale;

    // Use largest fitting scale if saved one doesn't fit.
    if (!rv_editor::rv_editor_shell_scale_fits(window, theme.scale)) {
        constexpr float candidates[] = { 2.0f, 1.5f, 1.0f };
        theme.scale = 1.0f;
        for (float candidate : candidates) {
            if (rv_editor::rv_editor_shell_scale_fits(window, candidate)) {
                theme.scale = candidate;
                break;
            }
        }
    }

    if (rv_editor_set_window_size_for_scale(window, theme.scale, true) != RV_OK) {
        std::fprintf(stderr, "3dmppc-editor: SetWindowMinimumSize/SetWindowSize: %s\n", SDL_GetError());
    }
    rv_editor::rv_editor_theme_apply(theme, ImGui::GetStyle());
    if (rv_editor_fonts_build(io, theme.scale) != RV_OK) {
        std::fprintf(stderr, "3dmppc-editor: cannot build the fonts\n");
        ImGui::DestroyContext();
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    ImGui_ImplSDL3_InitForSDLRenderer(window, renderer);
    ImGui_ImplSDLRenderer3_Init(renderer);
    rv_editor::rv_editor_icons_load(renderer);

    const std::filesystem::path layout_path = rv_editor::rv_editor_layout_file_path();
    // Heap-held: the shell's address goes to SDL's dialogs and to every pane.
    auto shell = std::make_unique<rv_editor::rv_editor_shell>();
    shell->window = window;
    shell->renderer = renderer;
    rv_editor::rv_editor_app_init(shell->app);
    shell->ui_scale = theme.scale;
    rv_editor::rv_editor_shell_load_layouts(*shell, layout_path, prefs.workspace);
    rv_editor::rv_editor_font_code_size_set(prefs.code_size);
    shell->app.game_scale = prefs.game_scale;
    if (!open_path.empty()) {
        rv_editor::rv_editor_shell_request_open(*shell, open_path);
    }

    while (!shell->quit_now) {
        if (rv_editor_poll(window, renderer, shell->window_focused) && rv_editor::rv_editor_shell_may_quit(*shell)) {
            break;
        }
        // Keyboard navigation is ImGui's use of arrows and Tab: off while a code
        // tile had the keyboard last frame, so those keys reach nvim.
        if (shell->app.text_focus) {
            io.ConfigFlags &= ~ImGuiConfigFlags_NavEnableKeyboard;
        } else {
            io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        }
        shell->app.text_focus = false;
        // View > UI Scale between frames: the style and every size follow.
        if (shell->ui_scale_request > 0.0f) {
            const float previous = theme.scale;
            theme.scale = shell->ui_scale_request;
            shell->ui_scale_request = 0.0f;
            // Rebuilt, not resized: the same pixels as a start at this scale.
            if (rv_editor_fonts_build(io, theme.scale) != RV_OK) {
                std::fprintf(stderr, "3dmppc-editor: cannot build the fonts for scale %g; keeping %g\n", theme.scale, previous);
                shell->app.log.add(rv_editor::rv_editor_log_source::editor, rv_editor::rv_editor_log_level::error,
                    "cannot build the fonts for the new UI scale; the previous scale stays");
                theme.scale = previous;
            }
            shell->ui_scale = theme.scale;
            if (rv_editor_set_window_size_for_scale(window, theme.scale, true) != RV_OK) {
                std::fprintf(stderr, "3dmppc-editor: SetWindowMinimumSize/SetWindowSize at scale %g: %s\n",
                    theme.scale, SDL_GetError());
            }
            rv_editor::rv_editor_theme_apply(theme, ImGui::GetStyle());
        }
        rv_editor::rv_editor_shell_update(*shell);
        ImGui_ImplSDLRenderer3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        rv_editor_display_pixels(window);
        ImGui::NewFrame();
        rv_editor_frame(*shell, theme);
        ImGui::Render();

        SDL_SetRenderDrawColor(renderer, (theme.window >> 16) & 0xff, (theme.window >> 8) & 0xff, theme.window & 0xff, 255);
        SDL_RenderClear(renderer);
        ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer);
        SDL_RenderPresent(renderer);
    }

    // No build and no runtime outlives the window; nor does a sound.
    rv_editor::rv_editor_app_shutdown(shell->app);
    rv_editor::rv_editor_sound_shutdown();

    std::string error;
    if (!layout_path.empty() && !rv_editor::rv_editor_shell_save_layouts(*shell, layout_path, error)) {
        std::fprintf(stderr, "3dmppc-editor: cannot save the layout: %s\n", error.c_str());
    }
    const rv_editor::rv_editor_prefs chosen{ rv_editor::rv_editor_font_code_size(), shell->app.game_scale,
        rv_editor::rv_editor_shell_workspace_key(*shell), shell->ui_scale };
    if (!prefs_path.empty() && !rv_editor::rv_editor_prefs_save(prefs_path, chosen, error)) {
        std::fprintf(stderr, "3dmppc-editor: cannot save the view settings: %s\n", error.c_str());
    }

    rv_editor::rv_editor_icons_free();
    ImGui_ImplSDLRenderer3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
