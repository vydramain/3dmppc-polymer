// Scene: the open scene document; without one, what a scene is and how to get one.
// A game draws a scene only through a loader.

#include "panes/rv_editor_panes.hpp"

#include <algorithm>
#include <string>
#include <vector>

#include "imgui.h"

#include "app/rv_editor_shell.hpp"
#include "panes/rv_editor_pane_tiled.hpp"
#include "panes/rv_editor_scene_draw.hpp"
#include "text/rv_editor_text.hpp"
#include "theme/rv_editor_theme_imgui.hpp"
#include "ui/rv_editor_widgets.hpp"
#include "ui/rv_editor_widgets_message.hpp"
#include "ui/rv_editor_widgets_picture.hpp"

namespace rv_editor
{

namespace
{

// The tabs work on a bare disc; its scene needs pdkkit, so the tile says why instead.
constexpr const char *no_pdkkit_text_id = "shell_pane.no_pdkkit";

// The longest viewport statuses, the numbers at their limit: two lines of each must fit the viewport at the minimum.
float rv_editor_scene_status_min_width()
{
    // The minimums are asked every frame; the answer changes only with the font or its size.
    static const ImFont *cached_font = nullptr;
    static float cached_size = 0.0f;
    static float cached_width = 0.0f;
    const ImFont *font = ImGui::GetIO().Fonts->Fonts[0];
    if (font == cached_font && font->LegacySize == cached_size) {
        return cached_width;
    }
    const double yaw = 999999.0, pitch = 999999.0, distance = 999999.9;
    const std::string mesh_error = "a.gltf: no triangles";
    std::vector<std::string> texts;
    for (const char *seek : { "", rv_editor_text("scene_viewport.seek_hint") }) {
        const std::string seek_msg = seek;
        const std::string camera =
            rv_editor_text_format("scene_viewport.camera_status", std::make_format_args(yaw, pitch, distance, seek_msg));
        for (const char *game : { rv_editor_text("scene_viewport.game_not_running"),
                 rv_editor_text("scene_viewport.game_read_scene"),
                 rv_editor_text("scene_viewport.game_not_read_scene") }) {
            texts.push_back(rv_editor_text_format("scene_viewport.status_line_format", std::make_format_args(camera, game)));
            texts.push_back(rv_editor_text_format("scene_viewport.mesh_placeholder_format",
                std::make_format_args(camera, game, mesh_error)));
        }
    }
    cached_font = font;
    cached_size = font->LegacySize;
    cached_width = rv_editor_picture_min_width(texts);
    return cached_width;
}

} // namespace

float rv_editor_scene_content_width()
{
    // The shelf and the viewport lay out in the window's inner width: both of its paddings, not only the one the minimum adds.
    const float pad = ImGui::GetStyle().WindowPadding.x;
    return std::max({ rv_editor_scene_none_width(),
        rv_editor_scene_new_width(),
        rv_editor_tiled_shelf_width(rv_editor_scene_toolbar_width()) + pad,
        rv_editor_scene_viewport_around().x + pad,
        // The viewport is what is left of the row after the wheels and labels round it.
        rv_editor_scene_status_min_width() + rv_editor_scene_viewport_around().x + pad });
}

rv_editor_size rv_editor_scene_minimum()
{
    const float spacing = ImGui::GetStyle().ItemSpacing.y;
    // The tab strip is counted whether or not a tab is open, so the minimum is the same in every state.
    const float above = rv_editor_tab_strip_height() + 2.0f * spacing + rv_editor_tiled_shelf_block() +
        rv_editor_tiled_box_block() + rv_editor_scene_viewport_around().y;
    return rv_editor_tiled_minimum(rv_editor_scene_content_width(), above);
}

void rv_editor_scene_say(rv_editor_app &app, const rv_editor_theme &theme, const rv_editor_ui_port &ui)
{
    const float box_width = std::max(rv_editor_scene_none_width(), rv_editor_scene_new_width());
    if (!rv_editor_scene_new_box(app, theme, ui, box_width)) {
        (void)rv_editor_message_box("##note", "", rv_editor_label_kind::normal, {}, theme, box_width);
    }
}

void rv_editor_pane_scene(rv_editor_app &app, SDL_Renderer *renderer, const rv_editor_theme &theme, const rv_editor_ui_port &ui)
{
    const float width = rv_editor_scene_content_width();
    const float box_width = std::max(rv_editor_scene_none_width(), rv_editor_scene_new_width());
    const rv_editor_scene_tabs &st = ui.state.scene_tabs;
    const bool bare = app.project.open && app.project.sdk == rv_editor_sdk_state::bare;
    // The question is asked in the scene's own tab: a picture, sound or model in front gives way to it.
    if (app.project.open && !bare && rv_editor_scene_new_open() && !st.tabs.empty() && st.front != 0) {
        ui.requests.push_back({ .kind = rv_editor_ui_request_kind::front_scene_tab, .scene_tab_index = 0 });
    }
    if (!rv_editor_scene_tabs_draw(app, renderer, theme, ui)) {
        return; // a picture, sound or model tab is in front; it drew its own content
    }
    const bool has_scene = app.project.open && !bare && app.scene != nullptr;
    if (has_scene) {
        rv_editor_scene_viewport(app, renderer, theme, ui);
        return;
    }
    if (bare) {
        (void)
            rv_editor_message_box("##none", rv_editor_text(no_pdkkit_text_id), rv_editor_label_kind::dim, {}, theme, box_width);
    } else if (!rv_editor_scene_new_box(app, theme, ui, box_width)) {
        const bool failed = !app.scene_error.empty();
        const std::string text = failed ? app.scene_error :
                                          std::string(rv_editor_text("pane_scene.no_scene_open")) + " " +
                rv_editor_text("pane_scene.game_loader_explanation");
        rv_editor_scene_none_box(app,
            text,
            failed ? rv_editor_label_kind::error : rv_editor_label_kind::normal,
            theme,
            box_width);
    }
    (void)rv_editor_tiled_area_begin("##scenes_area", width, theme);
    if (!bare) {
        rv_editor_scene_files_list(app, theme);
    }
    rv_editor_tiled_area_end(theme);
}

} // namespace rv_editor
