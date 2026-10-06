// Display size check: show error message when editor needs larger screen.

#include <cstdio>
#include <cmath>
#include <string>

#include <SDL3/SDL.h>

#include "pdk/rv_err.h"

#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_sdlrenderer3.h"

#include "app/rv_editor_shell.hpp"
#include "font/rv_editor_font.hpp"
#include "text/rv_editor_text.hpp"
#include "theme/rv_editor_theme.hpp"
#include "theme/rv_editor_theme_imgui.hpp"

namespace rv_editor
{

namespace
{

// Size of the error message window for measurement iterations.
constexpr int measure_window_width = 400;  // pixels, temporary measurement canvas
constexpr int measure_window_height = 240; // pixels, temporary measurement canvas

// Frames needed to stabilize window size calculation.
constexpr int measure_frames_count = 4; // iterations for size convergence

} // namespace

rv_editor_display_size rv_editor_get_display_size(SDL_Window *window)
{
    rv_editor_display_size result{};
    if (!window) {
        return result;
    }
    SDL_DisplayID display_id = SDL_GetDisplayForWindow(window);
    if (display_id == 0) {
        return result;
    }
    const SDL_DisplayMode *mode = SDL_GetCurrentDisplayMode(display_id);
    if (!mode) {
        return result;
    }
    result.w_pixels = static_cast<int>(mode->w * mode->pixel_density);
    result.h_pixels = static_cast<int>(mode->h * mode->pixel_density);
    return result;
}

// Print error message to stderr.
static void rv_editor_print_too_small_error(const rv_editor_display_size &display_size)
{
    std::fprintf(stderr,
        "3dmppc-editor: the display is %dx%d, the editor needs at least %dx%d\n",
        display_size.w_pixels,
        display_size.h_pixels,
        window_min_width,
        window_min_height);
}

// Draw message: "too_small.title", "too_small.required" and "too_small.current" (both formatted with width x height).
static void rv_editor_draw_too_small_message(const rv_editor_display_size &display_size)
{
    ImGui::TextUnformatted(rv_editor_text("too_small.title"));
    ImGui::Spacing();
    const int min_w = window_min_width;
    const int min_h = window_min_height;
    const std::string required = rv_editor_text_format("too_small.required", std::make_format_args(min_w, min_h));
    ImGui::TextUnformatted(required.c_str());
    const int w = display_size.w_pixels;
    const int h = display_size.h_pixels;
    const std::string current = rv_editor_text_format("too_small.current", std::make_format_args(w, h));
    ImGui::TextUnformatted(current.c_str());
}

// Forward declaration.
static int rv_editor_fonts_build_for_too_small(ImGuiIO &io, float scale);

void rv_editor_show_too_small_error(SDL_Window *&window, SDL_Renderer *&renderer, const rv_editor_display_size &display_size)
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.IniFilename = nullptr;

    rv_editor_theme theme = rv_editor_theme_olive;
    theme.scale = 1.0f;
    rv_editor_theme_apply(theme, ImGui::GetStyle());

    if (rv_editor_fonts_build_for_too_small(io, 1.0f) != RV_OK) {
        ImGui::DestroyContext();
        return;
    }

    ImGui_ImplSDL3_InitForSDLRenderer(window, renderer);
    ImGui_ImplSDLRenderer3_Init(renderer);

    // Measure message window size until stable (AlwaysAutoResize needs multiple frames).
    io.DisplaySize = ImVec2(measure_window_width, measure_window_height);
    io.DisplayFramebufferScale = ImVec2(1.0f, 1.0f);

    ImGuiWindowFlags window_flags = ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings;

    ImVec2 measured_size{};
    int window_w = 0;
    int window_h = 0;
    ImVec2 prev_size{};

    for (int frame = 0; frame < measure_frames_count; ++frame) {
        ImGui_ImplSDLRenderer3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();

        ImGui::SetNextWindowPos(ImVec2(0, 0));
        if (ImGui::Begin("##too_small", nullptr, window_flags)) {
            rv_editor_draw_too_small_message(display_size);
            measured_size = ImGui::GetWindowSize();
        }
        ImGui::End();

        ImGui::Render();

        // Stop when size stabilizes.
        if (measured_size.x == prev_size.x && measured_size.y == prev_size.y) {
            break;
        }
        prev_size = measured_size;
    }

    window_w = static_cast<int>(std::ceil(measured_size.x));
    window_h = static_cast<int>(std::ceil(measured_size.y));

    // Check if display is large enough.
    if (display_size.w_pixels < window_w || display_size.h_pixels < window_h) {
        ImGui_ImplSDLRenderer3_Shutdown();
        ImGui_ImplSDL3_Shutdown();
        ImGui::DestroyContext();
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        renderer = nullptr;
        window = nullptr;
        rv_editor_print_too_small_error(display_size);
        return;
    }

    // Resize and lock window, then show message loop.
    SDL_SetWindowSize(window, window_w, window_h);
    SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
    SDL_SetWindowResizable(window, false);
    SDL_SetWindowMinimumSize(window, window_w, window_h);

    bool quit = false;
    while (!quit) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL3_ProcessEvent(&event);
            if (event.type == SDL_EVENT_QUIT || event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) {
                quit = true;
            }
        }

        int w = 0;
        int h = 0;
        SDL_GetWindowSizeInPixels(window, &w, &h);
        io.DisplaySize = ImVec2(static_cast<float>(w), static_cast<float>(h));
        io.DisplayFramebufferScale = ImVec2(1.0f, 1.0f);

        ImGui_ImplSDLRenderer3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();

        window_flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoSavedSettings;

        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImVec2(static_cast<float>(w), static_cast<float>(h)));
        if (ImGui::Begin("##too_small", nullptr, window_flags)) {
            rv_editor_draw_too_small_message(display_size);
        }
        ImGui::End();

        ImGui::Render();

        SDL_SetRenderDrawColor(renderer,
            (theme.window >> channel_shift_red) & rgb_channel_mask,
            (theme.window >> channel_shift_green) & rgb_channel_mask,
            theme.window & rgb_channel_mask,
            alpha_opaque);
        SDL_RenderClear(renderer);
        ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer);
        SDL_RenderPresent(renderer);
    }

    // Cleanup.
    ImGui_ImplSDLRenderer3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    renderer = nullptr;
    window = nullptr;
}

static int rv_editor_fonts_build_for_too_small(ImGuiIO &io, float scale)
{
    const int err = rv_editor_fonts_add(*io.Fonts, scale);
    if (err != RV_OK) {
        return err;
    }
    ImGui::GetStyle().FontSizeBase = rv_editor_font_ui()->LegacySize;
    return RV_OK;
}

} // namespace rv_editor
