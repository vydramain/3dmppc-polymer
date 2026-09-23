// 3dmppc-editor entry point: one SDL3 window and one Dear ImGui frame loop.

#include <cstdio>

#include <SDL3/SDL.h>

#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_sdlrenderer3.h"

#include "theme/rv_editor_theme.hpp"
#include "theme/rv_editor_theme_imgui.hpp"

namespace
{

// One frame of the editor's UI. For now the widget catalog fills the window.
void rv_editor_frame()
{
    const ImGuiViewport *viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);

    constexpr ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings;
    ImGui::Begin("Widget Catalog", nullptr, flags);
    ImGui::TextUnformatted("Widget Catalog");
    ImGui::End();
}

// True once the user asked the window to close.
bool rv_editor_poll(SDL_Window *window)
{
    bool quit = false;
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
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

} // namespace

int main()
{
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::fprintf(stderr, "3dmppc-editor: SDL_Init: %s\n", SDL_GetError());
        return 1;
    }

    SDL_Window *window = nullptr;
    SDL_Renderer *renderer = nullptr;
    if (!SDL_CreateWindowAndRenderer("3dmppc-editor", 1280, 720, SDL_WINDOW_RESIZABLE, &window, &renderer)) {
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

    const rv_editor::rv_editor_theme &theme = rv_editor::rv_editor_theme_olive;
    rv_editor::rv_editor_theme_apply(theme, ImGui::GetStyle());

    ImGui_ImplSDL3_InitForSDLRenderer(window, renderer);
    ImGui_ImplSDLRenderer3_Init(renderer);

    while (!rv_editor_poll(window)) {
        ImGui_ImplSDLRenderer3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();
        rv_editor_frame();
        ImGui::Render();

        SDL_SetRenderDrawColor(renderer, (theme.window >> 16) & 0xff, (theme.window >> 8) & 0xff, theme.window & 0xff, 255);
        SDL_RenderClear(renderer);
        ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer);
        SDL_RenderPresent(renderer);
    }

    ImGui_ImplSDLRenderer3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
