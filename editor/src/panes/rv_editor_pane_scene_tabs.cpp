// The Scene tile's tabs: the scene itself in front, PNGs and sounds opened
// from Assets beside it, each a further tab (mirrors the Assets preview strip's
// look for a picture and a player, without touching that file).

#include "panes/rv_editor_panes.hpp"

#include <algorithm>
#include <string>
#include <system_error>
#include <vector>

#include "imgui.h"

#include "pdk/rv_err.h"

#include "app/rv_editor_shell.hpp"
#include "panes/rv_editor_asset_preview.hpp"
#include "text/rv_editor_text.hpp"
#include "theme/rv_editor_theme_imgui.hpp"
#include "ui/rv_editor_sound.hpp"
#include "ui/rv_editor_widgets.hpp"

namespace rv_editor
{

namespace
{

// Picture file extension.
constexpr std::string_view png_extension = ".png";

// Sound file extensions.
constexpr std::string_view wav_extension = ".wav";
constexpr std::string_view pcm_extension = ".pcm";

// Path escape indicator: relative path goes outside project root.
constexpr std::string_view path_escape_indicator = "..";

// The last failed play, kept until the front tab moves to a different file.
struct rv_editor_scene_sound_error
{
    std::filesystem::path path;
    std::string text;
};

rv_editor_scene_sound_error rv_editor_scene_sound_last_error;

// A path relative to the project root when it is under it, else its file name.
std::string rv_editor_scene_tab_rel(const rv_editor_app &app, const std::filesystem::path &path)
{
    std::error_code ec;
    const std::filesystem::path rel = std::filesystem::relative(path, app.project.root, ec);
    if (ec || rel.empty() || rel.native().starts_with(std::filesystem::path(path_escape_indicator).native())) {
        return path.filename().string();
    }
    return rel.generic_string();
}

void rv_editor_scene_tab_picture(rv_editor_app &app, SDL_Renderer *renderer, const rv_editor_scene_tab &tab)
{
    rv_editor_asset a;
    a.path = tab.path;
    a.rel = rv_editor_scene_tab_rel(app, tab.path);
    const rv_editor_icon picture = rv_editor_asset_picture(renderer, a);
    if (picture.id == ImTextureID{}) {
        const std::string error_msg = rv_editor_text_format("pane_scene_tabs.error_picture_load",
            std::make_format_args(a.rel));
        ImGui::TextWrapped("%s", error_msg.c_str());
        return;
    }
    const std::string picture_info = rv_editor_text_format("pane_scene_tabs.picture_info",
        std::make_format_args(picture.w, picture.h, a.rel));
    ImGui::Text("%s", picture_info.c_str());
    const ImVec2 room = ImGui::GetContentRegionAvail();
    ImGui::Image(picture.id, rv_editor_fit_picture(picture, room.x, room.y));
}

void rv_editor_scene_tab_sound(const rv_editor_scene_tab &tab, const rv_editor_theme &theme)
{
    ImGui::Text("%s", tab.path.filename().string().c_str());
    const double sound_seconds = tab.sound_seconds;
    ImGui::Text("%s", rv_editor_text_format("pane_scene_tabs.sound_seconds", std::make_format_args(sound_seconds)).c_str());
    const bool playing_this = rv_editor_sound_playing() && rv_editor_sound_path() == tab.path;
    if (rv_editor_button(rv_editor_text("pane_scene_tabs.button_play"), theme)) {
        std::string error;
        if (rv_editor_sound_play(tab.path, error) == RV_OK) {
            rv_editor_scene_sound_last_error = {};
        } else {
            rv_editor_scene_sound_last_error = { tab.path, error };
        }
    }
    ImGui::SameLine();
    const rv_editor_state stop_state{ rv_editor_look::live,
        playing_this ? nullptr : rv_editor_text("pane_scene_tabs.tooltip_nothing_playing") };
    if (rv_editor_button(rv_editor_text("pane_scene_tabs.button_stop"), theme, stop_state)) {
        rv_editor_sound_stop();
    }
    if (playing_this) {
        ImGui::SameLine();
        ImGui::TextDisabled("%s", rv_editor_text("pane_scene_tabs.status_playing"));
    }
    if (rv_editor_scene_sound_last_error.path == tab.path && !rv_editor_scene_sound_last_error.text.empty()) {
        ImGui::TextWrapped("%s", rv_editor_scene_sound_last_error.text.c_str());
    }
}

// Closes tabs[at] (0-based into app.scene_tabs.tabs); stops it first if it is a
// playing sound; brings the scene forward if it was in front.
void rv_editor_scene_tab_close(rv_editor_app &app, size_t at)
{
    rv_editor_scene_tabs &st = app.scene_tabs;
    const rv_editor_scene_tab &tab = st.tabs[at];
    if (tab.kind == rv_editor_scene_tab_kind::sound && rv_editor_sound_playing() && rv_editor_sound_path() == tab.path) {
        rv_editor_sound_stop();
    }
    const int closed_front = static_cast<int>(at) + 1;
    st.tabs.erase(st.tabs.begin() + static_cast<std::ptrdiff_t>(at));
    if (st.front == closed_front) {
        st.front = 0;
    } else if (st.front > closed_front) {
        st.front -= 1;
    }
}

} // namespace

void rv_editor_app_scene_tab_open(rv_editor_app &app, const std::filesystem::path &path)
{
    const std::string ext = path.extension().string();
    rv_editor_scene_tab_kind kind;
    if (ext == png_extension) {
        kind = rv_editor_scene_tab_kind::picture;
    } else if (ext == wav_extension || ext == pcm_extension) {
        kind = rv_editor_scene_tab_kind::sound;
    } else {
        return;
    }
    rv_editor_scene_tabs &st = app.scene_tabs;
    for (size_t i = 0; i < st.tabs.size(); ++i) {
        if (st.tabs[i].path == path) {
            st.front = static_cast<int>(i) + 1;
            return;
        }
    }
    rv_editor_scene_tab tab;
    tab.path = path;
    tab.kind = kind;
    if (kind == rv_editor_scene_tab_kind::sound) {
        rv_editor_asset a;
        a.path = path;
        std::error_code ec;
        a.size = std::filesystem::file_size(path, ec);
        tab.sound_seconds = rv_editor_asset_sound_seconds(a);
    }
    st.tabs.push_back(tab);
    st.front = static_cast<int>(st.tabs.size());
}

bool rv_editor_scene_tabs_draw(rv_editor_app &app, SDL_Renderer *renderer, const rv_editor_theme &theme)
{
    rv_editor_scene_tabs &st = app.scene_tabs;
    if (st.tabs.empty()) {
        return true;
    }
    std::vector<std::string> text;
    const char *scene_tab_label = rv_editor_text("pane_scene_tabs.tab_scene");
    text.push_back(app.scene != nullptr ? rv_editor_app_scene_name(app) : std::string(scene_tab_label));
    for (const rv_editor_scene_tab &tab : st.tabs) {
        text.push_back(tab.path.filename().string());
    }
    std::vector<const char *> labels;
    for (const std::string &s : text) {
        labels.push_back(s.c_str());
    }
    st.front = std::clamp(st.front, 0, static_cast<int>(st.tabs.size()));
    const ImVec2 row_min = ImGui::GetCursorScreenPos();
    rv_editor_tab_strip("##scene_tabs", labels.data(), static_cast<int>(labels.size()), &st.front, theme);

    // A right click on the tabs: Close Tab; the scene tab (index 0) has none.
    if (ImGui::IsMouseHoveringRect(row_min, ImGui::GetItemRectMax()) && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
        ImGui::OpenPopup("##scene_tabmenu");
    }
    rv_editor_menu_style_push();
    if (ImGui::BeginPopup("##scene_tabmenu")) {
        const bool closable = st.front > 0;
        if (ImGui::MenuItem(rv_editor_text("pane_scene_tabs.menu_close_tab"), nullptr, false, closable)) {
            rv_editor_scene_tab_close(app, static_cast<size_t>(st.front - 1));
        }
        if (!closable && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            ImGui::SetTooltip("%s", rv_editor_text("pane_scene_tabs.tooltip_scene_stays_front"));
        }
        ImGui::EndPopup();
    }
    rv_editor_menu_style_pop();

    if (st.front == 0) {
        // rv_editor_flow's first call below looks at the last item's rect to
        // decide whether to stay on this row; without a full-width item here
        // it would still see a tab and pull the viewport toolbar up onto it.
        ImGui::Dummy(ImVec2(ImGui::GetContentRegionAvail().x, 0.0f));
        return true;
    }
    const rv_editor_scene_tab &front = st.tabs[static_cast<size_t>(st.front - 1)];
    if (front.kind == rv_editor_scene_tab_kind::picture) {
        rv_editor_scene_tab_picture(app, renderer, front);
    } else {
        rv_editor_scene_tab_sound(front, theme);
    }
    return false;
}

} // namespace rv_editor
