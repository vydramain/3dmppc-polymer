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
#include "pdk/rv_err.h"
#include "text/rv_editor_text.hpp"
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

// Status line: dimension separator (width x height).
constexpr std::string_view dimension_sep = "x";

// Status line: separator between dimensions and scale text.
constexpr std::string_view dimensions_scale_sep = "  ";

// File path and error text separator when saving screenshot.
constexpr std::string_view error_kv_sep = ": ";

// Game picture tint color when console disconnected, hung, timeout, or stale: white semi-transparent.
constexpr uint32_t warn_picture_tint = IM_COL32(255, 255, 255, 96);

// Keyboard capture border thickness: drawn when game has focus and captures input.
constexpr float frame_thickness_px = 2.0f;

// Pixel format: bytes per pixel for SDL ARGB8888 format.
constexpr size_t bytes_per_pixel_argb8888 = 4;

// Fit, Integer, 1x, 2x, 3x (View > Game Scale), then Run: one shelf, same in
// every state, so it never changes the well's height between them. Burn's
// candidate view carries its own Run Candidate here instead of the dev Run.
void rv_editor_game_modes(rv_editor_app &app, const rv_editor_theme &theme)
{
    constexpr rv_editor_game_scale modes[] = { rv_editor_game_scale::fit,
        rv_editor_game_scale::integer,
        rv_editor_game_scale::x1,
        rv_editor_game_scale::x2,
        rv_editor_game_scale::x3 };
    constexpr const char *ids[] = { "pane_game.scale_fit",
        "pane_game.scale_integer",
        "pane_game.scale_1x",
        "pane_game.scale_2x",
        "pane_game.scale_3x" };
    for (size_t i = 0; i < std::size(modes); ++i) {
        const char *label = rv_editor_text(ids[i]);
        if (i > 0) {
            rv_editor_flow(rv_editor_button_width(label));
        }
        bool on = app.game_scale == modes[i];
        if (rv_editor_toggle(label, &on, theme)) {
            app.game_scale = modes[i];
        }
    }
    if (app.release_view) {
        const char *run_candidate_text = rv_editor_text("pane_game.run_candidate");
        rv_editor_flow(rv_editor_button_width(run_candidate_text));
        if (rv_editor_button(run_candidate_text, theme, { rv_editor_look::live, rv_editor_app_why_not_run_candidate(app) })) {
            rv_editor_app_run_candidate(app);
        }
        return;
    }
    const char *run_text = rv_editor_text("pane_game.run");
    rv_editor_flow(rv_editor_button_width(run_text));
    if (rv_editor_button(run_text, theme, { rv_editor_look::live, rv_editor_app_why_not_run(app) })) {
        rv_editor_app_run(app);
    }
}

// "Fit 2.37x", "Integer 2x", "3x, lowered to 2x to fit".
std::string rv_editor_game_scale_text(rv_editor_game_scale mode, const rv_editor_game_view &view)
{
    const float scale = view.scale;
    switch (mode) {
    case rv_editor_game_scale::fit:
        return rv_editor_text_format("pane_game.scale_fit_text", std::make_format_args(scale));
    case rv_editor_game_scale::integer:
        if (view.reduced) {
            return rv_editor_text_format("pane_game.scale_integer_reduced", std::make_format_args(scale));
        } else {
            return rv_editor_text_format("pane_game.scale_integer_normal", std::make_format_args(scale));
        }
    default: {
        const char *name = rv_editor_game_scale_name(mode);
        if (view.reduced) {
            return rv_editor_text_format("pane_game.scale_other_reduced", std::make_format_args(name, scale));
        } else {
            return rv_editor_text_format("pane_game.scale_other_normal", std::make_format_args(name));
        }
    }
    }
}

// The status line's colour: normal for a live or plain-stopped line, warn for a
// stale/disconnected frame, muted while no frame has ever arrived yet.
enum class rv_editor_game_line {
    normal,
    warn,
    muted
};

// One status line, then the picture area to the tile's bottom edge, dark, with the
// frame placed as Game Scale says once a texture exists; empty otherwise. Every
// state routes through here so this-frame's area is the same shape regardless of
// what the state has to show (game-steady).
void rv_editor_game_picture(rv_editor_app &app,
    const rv_editor_theme &theme,
    std::string status,
    rv_editor_game_line line,
    bool clickable)
{
    const bool have = rv_editor_game_texture != nullptr && rv_editor_game_frame != 0;
    ImVec2 area = ImGui::GetContentRegionAvail();
    area.y -= ImGui::GetTextLineHeightWithSpacing();
    rv_editor_game_view view{};
    if (have) {
        view = rv_editor_game_place(static_cast<int>(rv_editor_game_w),
            static_cast<int>(rv_editor_game_h),
            area.x,
            area.y,
            app.game_scale);
        status = std::to_string(rv_editor_game_w) + dimension_sep.data() + std::to_string(rv_editor_game_h) +
            dimensions_scale_sep.data() + rv_editor_game_scale_text(app.game_scale, view) + dimensions_scale_sep.data() +
            status;
    }
    if (line == rv_editor_game_line::warn) {
        ImGui::PushStyleColor(ImGuiCol_Text, rv_editor_col(theme.warning));
    } else if (line == rv_editor_game_line::muted) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    }
    ImGui::TextUnformatted(status.c_str());
    if (line != rv_editor_game_line::normal) {
        ImGui::PopStyleColor();
    }
    ImGui::SetItemTooltip("%s", status.c_str());

    // The whole area is dark, so what the frame leaves over is not the tile's olive.
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    ImDrawList *dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p0, ImVec2(p0.x + area.x, p0.y + area.y), rv_editor_col(theme.code_base));
    if (area.x < 1.0f || area.y < 1.0f) {
        return;
    }
    // A click on the picture takes the keyboard; the frame loop sends the keys
    // (rv_editor_shell_game_input). Not every state has a game to send them to.
    if (clickable) {
        if (ImGui::InvisibleButton("##game", area)) {
            app.game_captured = true;
        }
    } else {
        ImGui::Dummy(area);
    }
    if (!have) {
        return;
    }
    const ImVec2 i0(p0.x + view.x, p0.y + view.y);
    const ImVec2 i1(i0.x + view.w, i0.y + view.h);
    dl->AddImage(ImTextureID(reinterpret_cast<intptr_t>(rv_editor_game_texture)),
        i0,
        i1,
        ImVec2(0.0f, 0.0f),
        ImVec2(1.0f, 1.0f),
        line == rv_editor_game_line::warn ? warn_picture_tint : IM_COL32_WHITE);
    if (app.game_captured) {
        dl->AddRect(ImVec2(i0.x - 1, i0.y - 1),
            ImVec2(i1.x + 1, i1.y + 1),
            rv_editor_col(theme.selection),
            0.0f,
            frame_thickness_px);
    }
}

// Appends " stale: the last frame of session N, <state>" to status when a frame
// from an ended session is still on the texture, marked as such rather than
// looking like the game; returns whether it did.
bool rv_editor_game_join_stale(const rv_editor_app &app, std::string &status)
{
    const rv_editor_session &s = app.session;
    if (rv_editor_game_texture == nullptr || rv_editor_game_frame == 0 || s.number() == 0) {
        return false;
    }
    const uint64_t session_num = s.number();
    const char *state_name = rv_editor_run_state_name(s.state());
    const std::string stale_msg =
        rv_editor_text_format("pane_game.stale_session", std::make_format_args(session_num, state_name));
    status += stale_msg;
    return true;
}

// Burn's tile: the shelf's Run Candidate and, once a candidate's image runs, its
// own frame. The unpacked development build never stands in for it (README),
// whether stopped or a development session is live.
void rv_editor_game_candidate_tile(rv_editor_app &app, const rv_editor_theme &theme)
{
    std::string status;
    if (app.release.candidates.empty()) {
        status = rv_editor_text("pane_game.no_candidate");
    } else {
        const uint32_t candidate_num = app.release.candidates[app.release.selected].number;
        status = rv_editor_text_format("pane_game.candidate_image", std::make_format_args(candidate_num));
    }
    const bool stale = rv_editor_game_join_stale(app, status);
    rv_editor_game_picture(app, theme, status, stale ? rv_editor_game_line::warn : rv_editor_game_line::normal, false);
}

} // namespace

int rv_editor_game_capture(rv_editor_app &app, const std::filesystem::path &path, std::string &error)
{
    // Only a frame this session's console wrote: the pixels a stopped or older one left are not it.
    if (!app.session.live() || app.session.frame_memory().fd() != rv_editor_game_fd || rv_editor_game_frame == 0) {
        error = rv_editor_text("pane_game.error_no_frame");
        return RV_ERR_NOENT;
    }
    SDL_Surface *surface = SDL_CreateSurfaceFrom(static_cast<int>(rv_editor_game_w),
        static_cast<int>(rv_editor_game_h),
        SDL_PIXELFORMAT_ARGB8888,
        rv_editor_game_pixels.data(),
        static_cast<int>(rv_editor_game_w * bytes_per_pixel_argb8888));
    if (surface == nullptr) {
        error = SDL_GetError();
        return RV_ERR_IO;
    }
    const bool saved = SDL_SavePNG(surface, path.c_str());
    if (!saved) {
        error = path.string() + error_kv_sep.data() + SDL_GetError();
    }
    SDL_DestroySurface(surface);
    return saved ? RV_OK : RV_ERR_IO;
}

void rv_editor_pane_game(rv_editor_app &app, SDL_Renderer *renderer, const rv_editor_theme &theme)
{
    rv_editor_session &s = app.session;
    app.game_drawn = true;
    // Chrome this pane adds around the picture: the shelf, the well's own padding
    // on every side, and the status line, measured rather than listed, from the
    // leaf's content region before the shelf and this frame's picture area below.
    const ImVec2 region = ImGui::GetContentRegionAvail();
    const float top = ImGui::GetCursorPosY();

    rv_editor_shelf_begin("##shelf", theme);
    rv_editor_game_modes(app, theme);
    rv_editor_shelf_end();
    const float shelf_tall = ImGui::GetCursorPosY() - top;

    // The well fills what the shelf leaves, no scrollbar and no wheel scroll
    // (the Game tile never scrolls): status line and picture, or the stopped
    // text, all measured inside it.
    rv_editor_well_begin("##well", ImVec2(0, 0), theme, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    app.game_area = { static_cast<int32_t>(avail.x), static_cast<int32_t>(avail.y - ImGui::GetTextLineHeightWithSpacing()) };

    const auto body = [&]() {
        if (!s.live()) {
            app.game_captured = false;
            if (!app.project.open) {
                rv_editor_open_project_row(app, theme);
                return;
            }
            if (app.release_view && app.release.player != nullptr) {
                // The player draws in a window of its own: here only what the editor knows.
                const rv_editor_release &r = app.release;
                const auto seconds =
                    std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - r.player_started)
                        .count();
                const uint32_t number =
                    r.player_candidate >= 0 ? r.candidates[static_cast<size_t>(r.player_candidate)].number : 0;
                const int player_pid = static_cast<int>(r.player->pid());
                const long long elapsed_secs = static_cast<long long>(seconds);
                const std::string external_msg =
                    rv_editor_text_format("pane_game.external_player", std::make_format_args(number, player_pid, elapsed_secs));
                ImGui::TextWrapped("%s", external_msg.c_str());
                const char *button_text =
                    r.player_stopped ? rv_editor_text("pane_game.kill_player") : rv_editor_text("pane_game.stop_player");
                if (rv_editor_button(button_text, theme)) {
                    rv_editor_app_stop_player(app);
                }
                ImGui::SetItemTooltip("%s", rv_editor_text("pane_game.stop_player_tooltip"));
                return;
            }
            if (app.release_view) {
                rv_editor_game_candidate_tile(app, theme);
                return;
            }
            // Stopped: what the shelf's Run starts; a stale frame, if one is left,
            // joins the same line rather than opening a second one.
            std::string status;
            if (!rv_editor_app_run_builds(app)) {
                const uint32_t build_num = app.build.last_success()->number;
                status = rv_editor_text_format("pane_game.stopped_builds", std::make_format_args(build_num));
            } else {
                status = rv_editor_text("pane_game.stopped_rebuilds");
            }
            const bool stale = rv_editor_game_join_stale(app, status);
            rv_editor_game_picture(app, theme, status, stale ? rv_editor_game_line::warn : rv_editor_game_line::normal, false);
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
                rv_editor_game_texture = SDL_CreateTexture(renderer,
                    SDL_PIXELFORMAT_ARGB8888,
                    SDL_TEXTUREACCESS_STREAMING,
                    static_cast<int>(w),
                    static_cast<int>(h));
                if (rv_editor_game_texture != nullptr) {
                    SDL_SetTextureScaleMode(rv_editor_game_texture, SDL_SCALEMODE_NEAREST);
                }
                rv_editor_game_w = w;
                rv_editor_game_h = h;
            }
            if (rv_editor_game_texture != nullptr) {
                SDL_UpdateTexture(rv_editor_game_texture,
                    nullptr,
                    rv_editor_game_pixels.data(),
                    static_cast<int>(w * bytes_per_pixel_argb8888));
            }
            rv_editor_game_frame = f;
        }

        // The texture may still hold the last session's frame: not this one's.
        if (rv_editor_game_texture == nullptr || rv_editor_game_frame == 0) {
            rv_editor_game_picture(app, theme, rv_editor_text("pane_game.frame_waiting"), rv_editor_game_line::muted, false);
            return;
        }

        // A console that stopped answering leaves a frame that is no longer the game's.
        const char *state = nullptr;
        rv_editor_game_line line = rv_editor_game_line::normal;

        if (s.state() == rv_editor_run_state::disconnected) {
            state = rv_editor_text("pane_game.console_disconnected");
            line = rv_editor_game_line::warn;
            app.game_captured = false;
        } else if (s.state() == rv_editor_run_state::stopping && s.hung()) {
            state = rv_editor_text("pane_game.not_stopping");
            line = rv_editor_game_line::warn;
        } else if (s.state() == rv_editor_run_state::stopping) {
            state = rv_editor_text("pane_game.state_stopping");
        } else if (s.uncertain()) {
            state = rv_editor_text("pane_game.timeout");
            line = rv_editor_game_line::warn;
        } else if (s.state() == rv_editor_run_state::paused) {
            state = rv_editor_text("pane_game.state_paused");
        } else if (s.state() == rv_editor_run_state::pausing || s.state() == rv_editor_run_state::stepping ||
            s.state() == rv_editor_run_state::resuming || s.state() == rv_editor_run_state::starting) {
            state = rv_editor_run_state_name(s.state());
        } else if (s.state() == rv_editor_run_state::running) {
            if (app.game_captured) {
                state = rv_editor_text("pane_game.playing_keyboard");
            } else {
                state = rv_editor_text("pane_game.click_to_play");
            }
        } else {
            state = rv_editor_run_state_name(s.state());
        }

        const auto frame = s.frame();
        rv_editor_game_picture(app,
            theme,
            rv_editor_text_format("pane_game.frame_status", std::make_format_args(frame, state)),
            line,
            s.state() != rv_editor_run_state::disconnected);

        // Shift+Esc or focus elsewhere gives the keyboard back; Shift+Esc never reaches the game.
        if (app.game_captured &&
            (ImGui::IsKeyChordPressed(ImGuiMod_Shift | ImGuiKey_Escape) ||
                !ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows | ImGuiFocusedFlags_RootAndChildWindows))) {
            app.game_captured = false;
        }
    };
    body();
    // The tile's own minimum: the frame at 1x plus the measured chrome when
    // screen size is known, else just the shelf and status line.
    app.game_need = rv_editor_size{ 0, static_cast<int32_t>(shelf_tall + ImGui::GetTextLineHeightWithSpacing()) };
    if (app.project.screen_w > 0 && app.project.screen_h > 0) {
        app.game_need = rv_editor_size{ static_cast<int32_t>(app.project.screen_w) +
                std::max(0, static_cast<int32_t>(region.x) - app.game_area.w),
            static_cast<int32_t>(app.project.screen_h) + std::max(0, static_cast<int32_t>(region.y) - app.game_area.h) };
    }
    rv_editor_well_end();
}

} // namespace rv_editor
