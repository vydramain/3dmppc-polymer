// The Game tile: the console's own frame scaled as
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

// One status line, then the picture area to the tile's bottom edge, dark, with the
// frame placed as Game Scale says. A stale frame is dimmed and takes no click.
void rv_editor_game_picture(rv_editor_app &app, const rv_editor_theme &theme, const std::string &status, bool stale)
{
    ImVec2 area = ImGui::GetContentRegionAvail();
    area.y -= ImGui::GetTextLineHeightWithSpacing();
    const rv_editor_game_view view = rv_editor_game_place(static_cast<int>(rv_editor_game_w),
        static_cast<int>(rv_editor_game_h), area.x, area.y, app.game_scale);
    const std::string line = std::to_string(rv_editor_game_w) + "x" + std::to_string(rv_editor_game_h) + "  " +
        rv_editor_game_scale_text(app.game_scale, view) + "  " + status;
    if (stale) {
        ImGui::PushStyleColor(ImGuiCol_Text, rv_editor_col(theme.warning));
    }
    ImGui::TextUnformatted(line.c_str());
    if (stale) {
        ImGui::PopStyleColor();
    }

    // The whole area is dark, so what the frame leaves over is not the tile's olive.
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    ImDrawList *dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p0, ImVec2(p0.x + area.x, p0.y + area.y), rv_editor_col(theme.code_base));
    if (area.x < 1.0f || area.y < 1.0f) {
        return;
    }
    // A click on the picture takes the keyboard; the frame loop sends the keys
    // (rv_editor_shell_game_input). A stale one has no game to send them to.
    if (stale) {
        ImGui::Dummy(area);
    } else if (ImGui::InvisibleButton("##game", area)) {
        app.game_captured = true;
    }
    const ImVec2 i0(p0.x + view.x, p0.y + view.y);
    const ImVec2 i1(i0.x + view.w, i0.y + view.h);
    dl->AddImage(ImTextureID(reinterpret_cast<intptr_t>(rv_editor_game_texture)), i0, i1, ImVec2(0.0f, 0.0f),
        ImVec2(1.0f, 1.0f), stale ? IM_COL32(255, 255, 255, 96) : IM_COL32_WHITE);
    if (app.game_captured) {
        dl->AddRect(ImVec2(i0.x - 1, i0.y - 1), ImVec2(i1.x + 1, i1.y + 1), rv_editor_col(theme.selection), 0.0f,
            2.0f);
    }
}

// The last frame of a session that ended, marked as such rather than looking like
// the game (DEV-07); nothing when no frame of it arrived.
void rv_editor_game_stale(rv_editor_app &app, const rv_editor_theme &theme)
{
    const rv_editor_session &s = app.session;
    if (rv_editor_game_texture == nullptr || rv_editor_game_frame == 0 || s.number() == 0) {
        return;
    }
    rv_editor_game_picture(app, theme, std::string("stale: the last frame of session ") + std::to_string(s.number()) +
        ", " + rv_editor_run_state_name(s.state()), true);
}

// Burn's tile: Run Candidate and, once a candidate's image runs, its own frame.
// The unpacked development build never stands in for it (README), whether stopped
// or a development session is live.
void rv_editor_game_candidate_tile(rv_editor_app &app, const rv_editor_theme &theme)
{
    const std::string what = app.release.candidates.empty()
        ? "Stopped. No candidate yet: Build Candidate, then Run Candidate plays it here."
        : "Stopped. Run Candidate plays candidate #" +
            std::to_string(app.release.candidates[app.release.selected].number) + "'s image here.";
    ImGui::TextWrapped("%s", what.c_str());
    if (rv_editor_button("Run Candidate", theme, { rv_editor_look::live, rv_editor_app_why_not_run_candidate(app) })) {
        rv_editor_app_run_candidate(app);
    }
    rv_editor_game_stale(app, theme);
}

} // namespace

bool rv_editor_game_capture(rv_editor_app &app, const std::filesystem::path &path, std::string &error)
{
    // Only a frame this session's console wrote: the pixels a stopped or older one left are not it.
    if (!app.session.live() || app.session.frame_memory().fd() != rv_editor_game_fd || rv_editor_game_frame == 0) {
        error = "no frame of the running session has arrived yet";
        return false;
    }
    SDL_Surface *surface = SDL_CreateSurfaceFrom(static_cast<int>(rv_editor_game_w), static_cast<int>(rv_editor_game_h),
        SDL_PIXELFORMAT_ARGB8888, rv_editor_game_pixels.data(), static_cast<int>(rv_editor_game_w * 4));
    if (surface == nullptr) {
        error = SDL_GetError();
        return false;
    }
    const bool saved = SDL_SavePNG(surface, path.c_str());
    if (!saved) {
        error = path.string() + ": " + SDL_GetError();
    }
    SDL_DestroySurface(surface);
    return saved;
}

void rv_editor_pane_game(rv_editor_app &app, SDL_Renderer *renderer, const rv_editor_theme &theme)
{
    rv_editor_session &s = app.session;
    app.game_drawn = true;
    // Chrome this pane adds around the picture: the shelf, the well's own padding
    // on every side, and the status line, measured rather than listed, from the
    // leaf's content region before the shelf and this frame's picture area below.
    const ImVec2 region = ImGui::GetContentRegionAvail();

    rv_editor_shelf_begin("##shelf", theme);
    rv_editor_game_modes(app, theme);
    rv_editor_shelf_end();

    // The well fills what the shelf leaves, no scrollbar and no wheel scroll
    // (the Game tile never scrolls): status line and picture, or the stopped
    // text, all measured inside it.
    rv_editor_well_begin("##well", ImVec2(0, 0), theme, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    app.game_area = { static_cast<int32_t>(avail.x),
        static_cast<int32_t>(avail.y - ImGui::GetTextLineHeightWithSpacing()) };
    // The tile's own minimum: the frame at 1x plus that measured chrome, so the
    // picture never scales below 1x (fitting only ever grows the tile). Unset
    // before a picture has been measured once.
    app.game_need = app.game_area.w > 0 && app.game_area.h > 0
        ? rv_editor_size{ static_cast<int32_t>(app.project.screen_w) +
                std::max(0, static_cast<int32_t>(region.x) - app.game_area.w),
              static_cast<int32_t>(app.project.screen_h) +
                std::max(0, static_cast<int32_t>(region.y) - app.game_area.h) }
        : rv_editor_size{ 0, 0 };

    const auto body = [&]() {
        if (!s.live()) {
            app.game_captured = false;
            if (!app.project.open) {
                rv_editor_open_project_row(app, theme);
                return;
            }
            if (app.release_view && app.release.player != nullptr) {
                // The player draws in a window of its own: here only what the editor knows (BRN-04).
                const rv_editor_release &r = app.release;
                const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() -
                    r.player_started).count();
                const uint32_t number = r.player_candidate >= 0 ? r.candidates[static_cast<size_t>(r.player_candidate)].number
                                                                : 0;
                ImGui::TextWrapped("External player running candidate #%u in its own window: pid %d, %lld s.", number,
                    static_cast<int>(r.player->pid()), static_cast<long long>(seconds));
                if (rv_editor_button(r.player_stopped ? "Kill Player" : "Stop Player", theme)) {
                    rv_editor_app_stop_player(app);
                }
                ImGui::SetItemTooltip("The operator's act: the Player check does not pass from it");
                return;
            }
            if (app.release_view) {
                rv_editor_game_candidate_tile(app, theme);
                return;
            }
            // Stopped: what Run starts, and Run itself (or Build, with nothing built).
            const std::string target = !rv_editor_app_run_builds(app)
                ? "Stopped. Run starts build #" + std::to_string(app.build.last_success()->number) + " here."
                : "Stopped. Run builds the saved files, then starts that build here.";
            ImGui::TextWrapped("%s", target.c_str());
            if (rv_editor_button("Run", theme, { rv_editor_look::live, rv_editor_app_why_not_run(app) })) {
                rv_editor_app_run(app);
            }
            rv_editor_game_stale(app, theme);
            return;
        }

        // A development session (Code/Debug/Scene) can be live while Burn is open: its
        // frame is not a candidate's image, so Burn still shows the stopped tile.
        if (app.release_view && app.release.playing < 0) {
            app.game_captured = false;
            rv_editor_game_candidate_tile(app, theme);
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

        // The texture may still hold the last session's frame: not this one's.
        if (rv_editor_game_texture == nullptr || rv_editor_game_frame == 0) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
            ImGui::TextWrapped("Waiting for the console's first frame.");
            ImGui::PopStyleColor();
            return;
        }

        // A console that stopped answering leaves a frame that is no longer the game's.
        const bool lost = s.state() == rv_editor_run_state::disconnected;
        if (lost) {
            app.game_captured = false;
        }
        const char *state = lost ? "stale: the console stopped answering"
            : s.state() == rv_editor_run_state::paused ? "paused"
            : app.game_captured                        ? "playing: Shift+Esc gives the keyboard back"
                                                       : "click the picture to play";
        rv_editor_game_picture(app, theme, "frame " + std::to_string(s.frame()) + ", " + state, lost);

        // Shift+Esc or focus elsewhere gives the keyboard back; Shift+Esc never reaches the game.
        if (app.game_captured &&
            (ImGui::IsKeyChordPressed(ImGuiMod_Shift | ImGuiKey_Escape) ||
                !ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows | ImGuiFocusedFlags_RootAndChildWindows))) {
            app.game_captured = false;
        }
    };
    body();
    rv_editor_well_end();
}

} // namespace rv_editor
