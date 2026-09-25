// The Game tile (docs/adr/0006-game-frame.md): the console's own frame scaled as
// View > Game Scale says, and, while the tile holds the keyboard, its pad.

#include "panes/rv_editor_panes.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "imgui.h"
#include "panes/rv_editor_game_fit.hpp"
#include "pdk/cio/rv_isource.h"
#include "theme/rv_editor_theme_imgui.hpp"
#include "ui/rv_editor_widgets.hpp"

namespace rv_editor
{

namespace
{

SDL_Texture *rv_editor_game_texture = nullptr;
uint32_t rv_editor_game_w = 0, rv_editor_game_h = 0;
uint64_t rv_editor_game_frame = 0;
std::vector<uint32_t> rv_editor_game_pixels;
int rv_editor_game_fd = -1;

} // namespace

uint64_t rv_editor_game_keys()
{
    uint64_t buttons = 0;
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
    return buttons;
}

namespace
{

// Fit, Integer, 1x, 2x, 3x: the same choice as View > Game Scale.
void rv_editor_game_modes(rv_editor_app &app, const rv_editor_theme &theme)
{
    constexpr rv_editor_game_scale modes[] = { rv_editor_game_scale::fit, rv_editor_game_scale::integer,
        rv_editor_game_scale::x1, rv_editor_game_scale::x2, rv_editor_game_scale::x3 };
    constexpr const char *labels[] = { "Fit", "Integer", "1x", "2x", "3x" };
    for (size_t i = 0; i < std::size(modes); ++i) {
        if (i > 0) {
            rv_editor_flow(rv_editor_button_width(labels[i]));
        }
        bool on = app.game_scale == modes[i];
        if (rv_editor_toggle(labels[i], &on, theme)) {
            app.game_scale = modes[i];
        }
    }
}

// "Fit 2.37x", "Integer 2x", "3x, lowered to 2x to fit".
std::string rv_editor_game_scale_text(rv_editor_game_scale mode, const rv_editor_game_view &view)
{
    char buf[64];
    switch (mode) {
        case rv_editor_game_scale::fit: std::snprintf(buf, sizeof(buf), "Fit %.2fx", view.scale); break;
        case rv_editor_game_scale::integer:
            std::snprintf(buf, sizeof(buf), view.reduced ? "Integer: %.2fx, below 1x to fit" : "Integer %.0fx",
                view.scale);
            break;
        default:
            std::snprintf(buf, sizeof(buf), view.reduced ? "%s, lowered to %.2fx to fit" : "%s",
                rv_editor_game_scale_name(mode), view.scale);
            break;
    }
    return buf;
}

} // namespace

void rv_editor_pane_game(rv_editor_app &app, SDL_Renderer *renderer, const rv_editor_theme &theme)
{
    rv_editor_session &s = app.session;
    app.game_drawn = true;
    rv_editor_game_modes(app, theme);

    if (!s.live()) {
        app.game_captured = false;
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

    // One status line, then the picture area to the tile's bottom edge.
    ImVec2 area = ImGui::GetContentRegionAvail();
    area.y -= ImGui::GetTextLineHeightWithSpacing();
    const rv_editor_game_view view = rv_editor_game_place(static_cast<int>(rv_editor_game_w),
        static_cast<int>(rv_editor_game_h), area.x, area.y, app.game_scale);

    const char *state = s.state() == rv_editor_run_state::paused ? "paused"
        : app.game_captured ? "playing: Shift+Esc gives the keyboard back"
                            : "click the picture to play";
    const std::string line = std::to_string(rv_editor_game_w) + "x" + std::to_string(rv_editor_game_h) + "  " +
        rv_editor_game_scale_text(app.game_scale, view) + "  frame " + std::to_string(s.frame()) + ", " + state;
    ImGui::TextUnformatted(line.c_str());

    // The whole area is dark, so what the frame leaves over is not the tile's olive.
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    ImDrawList *dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p0, ImVec2(p0.x + area.x, p0.y + area.y), rv_editor_col(theme.code_base));
    if (area.x < 1.0f || area.y < 1.0f) {
        return;
    }
    // A click on the picture takes the keyboard; the frame loop sends the keys
    // (rv_editor_shell_game_input).
    if (ImGui::InvisibleButton("##game", area)) {
        app.game_captured = true;
    }
    const ImVec2 i0(p0.x + view.x, p0.y + view.y);
    const ImVec2 i1(i0.x + view.w, i0.y + view.h);
    dl->AddImage(ImTextureID(reinterpret_cast<intptr_t>(rv_editor_game_texture)), i0, i1);
    if (app.game_captured) {
        dl->AddRect(ImVec2(i0.x - 1, i0.y - 1), ImVec2(i1.x + 1, i1.y + 1), rv_editor_col(theme.selection), 0.0f,
            2.0f);
    }

    // Shift+Esc or focus elsewhere gives the keyboard back; Shift+Esc never reaches the game.
    if (app.game_captured &&
        (ImGui::IsKeyChordPressed(ImGuiMod_Shift | ImGuiKey_Escape) ||
            !ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows | ImGuiFocusedFlags_RootAndChildWindows))) {
        app.game_captured = false;
    }
}

} // namespace rv_editor
