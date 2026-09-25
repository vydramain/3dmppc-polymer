// The Game tile (docs/adr/0006-game-frame.md): the console's own frame and, while the tile holds the keyboard, its pad.

#include "panes/rv_editor_panes.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

#include "imgui.h"
#include "pdk/cio/rv_isource.h"

namespace rv_editor
{

namespace
{

SDL_Texture *rv_editor_game_texture = nullptr;
uint32_t rv_editor_game_w = 0, rv_editor_game_h = 0;
uint64_t rv_editor_game_frame = 0;
std::vector<uint32_t> rv_editor_game_pixels;
bool rv_editor_game_captured = false;
int rv_editor_game_fd = -1;

} // namespace

void rv_editor_pane_game(rv_editor_app &app, SDL_Renderer *renderer, const rv_editor_theme &theme)
{
    rv_editor_session &s = app.session;

    if (!s.live()) {
        rv_editor_game_captured = false;
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        ImGui::TextWrapped("No console is running: Run (F5) starts the game here.");
        ImGui::PopStyleColor();
        return;
    }

    // A new session is a new memory object whose frames count from 1 again.
    if (s.frame_memory().fd() != rv_editor_game_fd) {
        rv_editor_game_fd = s.frame_memory().fd();
        rv_editor_game_frame = 0;
    }

    uint64_t f = rv_editor_game_frame;
    uint32_t w = 0, h = 0;
    if (s.frame_memory().read(f, w, h, rv_editor_game_pixels)) {
        if (rv_editor_game_texture == nullptr || w != rv_editor_game_w || h != rv_editor_game_h) {
            if (rv_editor_game_texture != nullptr) {
                SDL_DestroyTexture(rv_editor_game_texture);
            }
            rv_editor_game_texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
                SDL_TEXTUREACCESS_STREAMING, static_cast<int>(w), static_cast<int>(h));
            if (rv_editor_game_texture != nullptr) {
                SDL_SetTextureScaleMode(rv_editor_game_texture, SDL_SCALEMODE_NEAREST);
            }
            rv_editor_game_w = w;
            rv_editor_game_h = h;
        }
        if (rv_editor_game_texture != nullptr) {
            SDL_UpdateTexture(rv_editor_game_texture, nullptr, rv_editor_game_pixels.data(),
                static_cast<int>(w * 4));
        }
        rv_editor_game_frame = f;
    }

    if (rv_editor_game_texture == nullptr) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        ImGui::TextWrapped("Waiting for the console's first frame.");
        ImGui::PopStyleColor();
        return;
    }

    // The state line goes above the picture, so a tight tile never cuts it off.
    if (s.state() == rv_editor_run_state::paused) {
        ImGui::TextUnformatted(("Paused, frame " + std::to_string(s.frame())).c_str());
    } else if (rv_editor_game_captured) {
        ImGui::TextUnformatted("Playing: Shift+Esc gives the keyboard back");
    } else {
        ImGui::TextUnformatted("Click the picture to play");
    }

    // Whole multiples only, nearest sampling: the console's pixels stay square (LAY-05).
    ImVec2 avail = ImGui::GetContentRegionAvail();
    int k = std::max(1, static_cast<int>(std::floor(
        std::min(avail.x / static_cast<float>(rv_editor_game_w), avail.y / static_cast<float>(rv_editor_game_h)))));
    ImVec2 size(static_cast<float>(rv_editor_game_w * k), static_cast<float>(rv_editor_game_h * k));

    ImGui::SetCursorPosX(ImGui::GetCursorPosX() +
        std::max(0.0f, (avail.x - size.x) * 0.5f));

    // A click on the picture takes the keyboard.
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    if (ImGui::InvisibleButton("##game", size)) {
        rv_editor_game_captured = true;
    }

    ImDrawList *dl = ImGui::GetWindowDrawList();
    dl->AddImage(ImTextureID(reinterpret_cast<intptr_t>(rv_editor_game_texture)), p0,
        ImVec2(p0.x + size.x, p0.y + size.y));

    if (rv_editor_game_captured) {
        ImVec2 p1(p0.x + size.x, p0.y + size.y);
        dl->AddRect(ImVec2(p0.x - 1, p0.y - 1), ImVec2(p1.x + 1, p1.y + 1),
            IM_COL32((theme.selection >> 16) & 0xff, (theme.selection >> 8) & 0xff,
                theme.selection & 0xff, 255),
            0.0f, 2.0f);
    }

    // Shift+Esc or focus elsewhere gives the keyboard back; Shift+Esc never reaches the game.
    if (rv_editor_game_captured &&
        (ImGui::IsKeyChordPressed(ImGuiMod_Shift | ImGuiKey_Escape) ||
            !ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows |
                ImGuiFocusedFlags_RootAndChildWindows))) {
        rv_editor_game_captured = false;
    }

    // Paused, released or elsewhere: all buttons up, so nothing stays held (GAM-04).
    uint64_t buttons = 0;
    if (rv_editor_game_captured && s.state() == rv_editor_run_state::running) {
        // The console's own keys (src/rv_pconsole/platform/sdl3/rv_pcwindow_sdl3.cpp).
        if (!ImGui::GetIO().KeyShift && ImGui::IsKeyDown(ImGuiKey_Escape)) {
            buttons |= RV_ISOURCE_MENU_BTTN_MENU;
        }
        if (ImGui::IsKeyDown(ImGuiKey_Tab)) {
            buttons |= RV_ISOURCE_MENU_BTTN_VIEW;
        }
        if (ImGui::IsKeyDown(ImGuiKey_Space) || ImGui::IsKeyDown(ImGuiKey_Z)) {
            buttons |= RV_ISOURCE_FRONT_BTTN_SOUTH;
        }
        if (ImGui::IsKeyDown(ImGuiKey_X)) {
            buttons |= RV_ISOURCE_FRONT_BTTN_EAST;
        }
        if (ImGui::IsKeyDown(ImGuiKey_C)) {
            buttons |= RV_ISOURCE_FRONT_BTTN_WEST;
        }
        if (ImGui::IsKeyDown(ImGuiKey_V)) {
            buttons |= RV_ISOURCE_FRONT_BTTN_NORTH;
        }
        if (ImGui::IsKeyDown(ImGuiKey_Q)) {
            buttons |= RV_ISOURCE_BUMPER_LEFT;
        }
        if (ImGui::IsKeyDown(ImGuiKey_E)) {
            buttons |= RV_ISOURCE_BUMPER_RIGHT;
        }
        if (ImGui::IsKeyDown(ImGuiKey_UpArrow)) {
            buttons |= RV_ISOURCE_DPAD_NORTH | RV_ISOURCE_DPAD_MOVE;
        }
        if (ImGui::IsKeyDown(ImGuiKey_DownArrow)) {
            buttons |= RV_ISOURCE_DPAD_SOUTH | RV_ISOURCE_DPAD_MOVE;
        }
        if (ImGui::IsKeyDown(ImGuiKey_LeftArrow)) {
            buttons |= RV_ISOURCE_DPAD_WEST | RV_ISOURCE_DPAD_MOVE;
        }
        if (ImGui::IsKeyDown(ImGuiKey_RightArrow)) {
            buttons |= RV_ISOURCE_DPAD_EAST | RV_ISOURCE_DPAD_MOVE;
        }
        app.text_focus = true;
    }
    s.pad(buttons, app.log);
}

} // namespace rv_editor
