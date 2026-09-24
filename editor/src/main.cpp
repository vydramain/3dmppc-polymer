// 3dmppc-editor entry point: one SDL3 window and one Dear ImGui frame loop.

#include <charconv>
#include <cstdio>
#include <filesystem>
#include <string>
#include <string_view>

#include <SDL3/SDL.h>

#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_sdlrenderer3.h"

#include "catalog/rv_editor_catalog.hpp"
#include "font/rv_editor_font.hpp"
#include "theme/rv_editor_theme.hpp"
#include "theme/rv_editor_theme_imgui.hpp"
#include "ui/rv_editor_icons.hpp"
#include "ui/rv_editor_widgets.hpp"

namespace
{

constexpr int rv_editor_scale_max = 8;

void rv_editor_pane_draw(rv_editor::rv_editor_pane_id, rv_editor::rv_editor_pane_kind kind,
    const rv_editor::rv_editor_theme &theme)
{
    if (kind == rv_editor::rv_editor_pane_kind::catalog) {
        rv_editor::rv_editor_catalog_draw(theme);
    } else if (kind == rv_editor::rv_editor_pane_kind::game) {
        ImGui::TextDisabled("Frame size unknown: no console is running.");
    } else {
        ImGui::TextDisabled("%s: not implemented yet.", rv_editor::rv_editor_pane_title(kind));
    }
}

rv_editor::rv_editor_workspace rv_editor_workspace_preset(rv_editor::rv_editor_layout_preset preset)
{
    rv_editor::rv_editor_workspace ws;
    rv_editor::rv_editor_layout_preset_make(preset, ws.panes, ws.layout);
    return ws;
}

// The saved layout, or Code when there is none or it cannot be read (LAY-06).
rv_editor::rv_editor_workspace rv_editor_workspace_load(const std::filesystem::path &path)
{
    rv_editor::rv_editor_workspace ws = rv_editor_workspace_preset(rv_editor::rv_editor_layout_preset::code);
    if (path.empty() || rv_editor::rv_editor_layout_load(path, ws.panes, ws.layout)) {
        return ws;
    }
    std::error_code ec;
    if (std::filesystem::exists(path, ec)) {
        std::fprintf(stderr, "3dmppc-editor: %s is not a layout this editor reads; starting from Code\n",
            path.c_str());
    }
    return ws;
}

// Layout and Window menus. Runs before the workspace is drawn, so a change here
// never lands under a reference the drawing holds.
void rv_editor_menu(rv_editor::rv_editor_workspace &ws)
{
    rv_editor::rv_editor_menu_style_push();
    if (!ImGui::BeginMainMenuBar()) {
        rv_editor::rv_editor_menu_style_pop();
        return;
    }
    if (ImGui::BeginMenu("Layout")) {
        constexpr rv_editor::rv_editor_layout_preset presets[] = { rv_editor::rv_editor_layout_preset::code,
            rv_editor::rv_editor_layout_preset::scene, rv_editor::rv_editor_layout_preset::debug,
            rv_editor::rv_editor_layout_preset::build };
        for (const rv_editor::rv_editor_layout_preset preset : presets) {
            if (ImGui::MenuItem(rv_editor::rv_editor_layout_preset_name(preset))) {
                ws = rv_editor_workspace_preset(preset);
            }
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Reset Layout")) {
            ws = rv_editor_workspace_preset(rv_editor::rv_editor_layout_preset::code);
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Window")) {
        if (ImGui::MenuItem("Widget Catalog")) {
            const bool focused = ws.focused_leaf < ws.layout.nodes.size() &&
                ws.layout.nodes[ws.focused_leaf].kind == rv_editor::rv_editor_tile_kind::leaf;
            const uint32_t leaf = focused ? ws.focused_leaf : ws.layout.root;
            const rv_editor::rv_editor_pane_id pane =
                rv_editor::rv_editor_pane_add(ws.panes, rv_editor::rv_editor_pane_kind::catalog);
            rv_editor::rv_editor_tile_insert(ws.layout, leaf, pane, rv_editor::rv_editor_tile_dock::tab);
            rv_editor::rv_editor_tile_activate(ws.layout, pane);
        }
        ImGui::EndMenu();
    }
    ImGui::EndMainMenuBar();
    rv_editor::rv_editor_menu_style_pop();
}

void rv_editor_usage(std::FILE *out)
{
    std::fprintf(out,
        "usage: 3dmppc-editor [-s|--scale N]\n"
        "  -s, --scale N   Integer UI scale, 1..%d. Default: 1. The editor draws one\n"
        "                  of its pixels per screen pixel unless this asks for more.\n",
        rv_editor_scale_max);
}

// Whole-number scale from the command line. False with exit_code set when the
// program should stop: 0 after --help, 2 after a bad argument.
bool rv_editor_args_parse(int argc, char **argv, int &scale, int &exit_code)
{
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            rv_editor_usage(stdout);
            exit_code = 0;
            return false;
        }

        std::string_view value;
        if (arg == "-s" || arg == "--scale") {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "3dmppc-editor: %s needs a value\n", argv[i]);
                exit_code = 2;
                return false;
            }
            value = argv[++i];
        } else if (arg.starts_with("--scale=")) {
            value = arg.substr(8);
        } else {
            std::fprintf(stderr, "3dmppc-editor: unknown argument '%s'\n", argv[i]);
            rv_editor_usage(stderr);
            exit_code = 2;
            return false;
        }

        int parsed = 0;
        const auto [end, ec] = std::from_chars(value.data(), value.data() + value.size(), parsed);
        if (ec != std::errc{} || end != value.data() + value.size() || parsed < 1 || parsed > rv_editor_scale_max) {
            std::fprintf(stderr, "3dmppc-editor: --scale takes a whole number 1..%d, not '%.*s'\n",
                rv_editor_scale_max, static_cast<int>(value.size()), value.data());
            exit_code = 2;
            return false;
        }
        scale = parsed;
    }
    return true;
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
void rv_editor_frame(rv_editor::rv_editor_workspace &ws, const rv_editor::rv_editor_theme &theme)
{
    // The background list is rendered first, and the backend resets sampling only
    // at the start of a render: one request here keeps the whole frame unsmoothed.
    ImGui::GetBackgroundDrawList()->AddCallback(ImGui::GetPlatformIO().DrawCallback_SetSamplerNearest, nullptr);
    rv_editor_menu(ws);

    // The status bar takes a row; the tiles get the rest.
    const ImGuiViewport *viewport = ImGui::GetMainViewport();
    const ImVec2 top = viewport->WorkPos;
    const ImVec2 size = viewport->WorkSize;
    const float bar = ImGui::GetFrameHeight() + 2.0f * ImGui::GetStyle().WindowPadding.y;
    if (rv_editor_bar_begin("##status", ImVec2(top.x, top.y + size.y - bar), ImVec2(size.x, bar))) {
        const char *const fields[] = { "Ready.", "Runtime: Stopped" };
        rv_editor::rv_editor_status_bar(fields, 2, theme);
    }
    ImGui::End();

    const rv_editor::rv_editor_rect area{ static_cast<int>(top.x), static_cast<int>(top.y),
        static_cast<int>(size.x), static_cast<int>(size.y - bar) };
    // The tiles get a host strip of their own, like the bars around them.
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    const bool host = rv_editor_bar_begin("##tiles", top, ImVec2(size.x, size.y - bar));
    ImGui::PopStyleVar();
    if (host) {
        rv_editor::rv_editor_workspace_draw(ws, theme, rv_editor_pane_draw, area);
    }
    ImGui::End();
}

// True once the user asked the window to close. Pointer coordinates are turned
// into render pixels first: ImGui works in pixels, not in the desktop's points.
bool rv_editor_poll(SDL_Window *window, SDL_Renderer *renderer)
{
    bool quit = false;
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        SDL_ConvertEventToRenderCoordinates(renderer, &event);
        ImGui_ImplSDL3_ProcessEvent(&event);
        if (event.type == SDL_EVENT_QUIT) {
            quit = true;
        }
        if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED && event.window.windowID == SDL_GetWindowID(window)) {
            quit = true;
        }
    }
    return quit;
}

// The backend reports the window in points and asks the renderer to scale by the
// display's density. The editor takes the window in pixels at density 1 instead,
// so no scale is applied that --scale did not ask for.
void rv_editor_display_pixels(SDL_Window *window)
{
    int w = 0;
    int h = 0;
    SDL_GetWindowSizeInPixels(window, &w, &h);
    ImGuiIO &io = ImGui::GetIO();
    io.DisplaySize = ImVec2(static_cast<float>(w), static_cast<float>(h));
    io.DisplayFramebufferScale = ImVec2(1.0f, 1.0f);
}

} // namespace

int main(int argc, char **argv)
{
    int scale = 1;
    int exit_code = 0;
    if (!rv_editor_args_parse(argc, argv, scale, exit_code)) {
        return exit_code;
    }

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
    SDL_SetRenderVSync(renderer, 1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    // The editor will keep its own layout file; ImGui writes none.
    io.IniFilename = nullptr;

    rv_editor::rv_editor_theme theme = rv_editor::rv_editor_theme_olive;
    theme.scale = scale;
    rv_editor::rv_editor_theme_apply(theme, ImGui::GetStyle());
    const ImFont *font = rv_editor::rv_editor_font_add(*io.Fonts, theme.scale);
    if (font == nullptr) {
        std::fprintf(stderr, "3dmppc-editor: cannot load the font %s\n", RV_EDITOR_FONT_PATH);
        ImGui::DestroyContext();
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    ImGui::GetStyle().FontSizeBase = font->LegacySize;

    ImGui_ImplSDL3_InitForSDLRenderer(window, renderer);
    ImGui_ImplSDLRenderer3_Init(renderer);
    rv_editor::rv_editor_icons_load(renderer);

    const std::filesystem::path layout_path = rv_editor::rv_editor_layout_file_path();
    rv_editor::rv_editor_workspace workspace = rv_editor_workspace_load(layout_path);

    while (!rv_editor_poll(window, renderer)) {
        ImGui_ImplSDLRenderer3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        rv_editor_display_pixels(window);
        ImGui::NewFrame();
        rv_editor_frame(workspace, theme);
        ImGui::Render();

        SDL_SetRenderDrawColor(renderer, (theme.window >> 16) & 0xff, (theme.window >> 8) & 0xff, theme.window & 0xff, 255);
        SDL_RenderClear(renderer);
        ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer);
        SDL_RenderPresent(renderer);
    }

    std::string error;
    if (!layout_path.empty() && !rv_editor::rv_editor_layout_save(layout_path, workspace.panes, workspace.layout, error)) {
        std::fprintf(stderr, "3dmppc-editor: cannot save the layout: %s\n", error.c_str());
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
