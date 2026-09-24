// 3dmppc-editor entry point: one SDL3 window and one Dear ImGui frame loop.

#include <charconv>
#include <cstdio>
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

namespace
{

constexpr int rv_editor_scale_max = 8;

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

// One frame of the editor's UI. For now the widget catalog fills the window.
void rv_editor_frame(const rv_editor::rv_editor_theme &theme)
{
    // The background list is rendered first, and the backend resets sampling only
    // at the start of a render: one request here keeps the whole frame unsmoothed.
    ImGui::GetBackgroundDrawList()->AddCallback(ImGui::GetPlatformIO().DrawCallback_SetSamplerNearest, nullptr);
    rv_editor::rv_editor_catalog_draw(theme);
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

    while (!rv_editor_poll(window, renderer)) {
        ImGui_ImplSDLRenderer3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        rv_editor_display_pixels(window);
        ImGui::NewFrame();
        rv_editor_frame(theme);
        ImGui::Render();

        SDL_SetRenderDrawColor(renderer, (theme.window >> 16) & 0xff, (theme.window >> 8) & 0xff, theme.window & 0xff, 255);
        SDL_RenderClear(renderer);
        ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer);
        SDL_RenderPresent(renderer);
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
