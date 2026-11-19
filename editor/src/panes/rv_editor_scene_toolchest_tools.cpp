// The Scene Toolchest's tools: the tool tiles, Snap with its step, and Grid.
#include "panes/rv_editor_panes.hpp"

#include <iterator>

#include "imgui.h"

#include "app/rv_editor_shell.hpp"
#include "panes/rv_editor_scene_draw.hpp"
#include "pdk/rv_err.h"
#include "pdklib/rv_logs/rv_logs.hpp"
#include "scene/rv_editor_scene.hpp"
#include "text/rv_editor_text.hpp"
#include "ui/rv_editor_widgets.hpp"
#include "ui/rv_editor_widgets_tile.hpp"

namespace rv_editor
{

namespace
{

constexpr float dropdown_width_factor = 5.0f;

// Snap steps in scene units. Each is rounded to raw when the camera or the dropdown takes it: 0.1 is raw 410
// (0.10009765625), and the drag snaps to that step.
constexpr const char *snap_step_labels[] = { "0.1", "0.25", "0.5", "1" };
constexpr double snap_step_values[] = { 0.1, 0.25, 0.5, 1.0 };
static_assert(std::size(snap_step_labels) == std::size(snap_step_values),
    "snap_step_labels and snap_step_values must have equal size");
// Default snap step choice index (0.25 matches camera_snap_step_default in editor/src/app/rv_editor_app_parts.hpp).
constexpr int default_snap_step_index = 1;

// The Toolchest tools in rv_editor_scene_tool order. The letters repeat the keys that pick them in
// rv_editor_scene_viewport.cpp; the ids are unique tile labels, never drawn.
struct rv_editor_scene_tool_option {
    const char *code;
    const char *id;
    const char *name_id;
};
constexpr rv_editor_scene_tool_option scene_tool_options[] = {
    { "Q", "##tool_select", "scene_tools.tool_select" },
    { "W", "##tool_move", "scene_tools.tool_move" },
    { "E", "##tool_rotate", "scene_tools.tool_rotate" },
    { "R", "##tool_scale", "scene_tools.tool_scale" },
};
static_assert(std::size(scene_tool_options) == static_cast<size_t>(rv_editor_scene_tool::scale) + 1,
    "scene_tool_options needs one entry per rv_editor_scene_tool");

// The dropdown item the camera's snap step is at, by raw match. Index is set only on a match: untouched on no match
// and on error (warning logged once); the caller pre-sets the default.
[[nodiscard]] int rv_editor_snap_step_index(const rv_editor_scene_camera &cam, int &index)
{
    // Once per process: the dropdown is drawn every frame.
    static bool reported = false;
    for (int i = 0; i < std::ssize(snap_step_values); ++i) {
        bool at = false;
        if (rv_editor_camera_snap_at(cam, snap_step_values[i], at) != RV_OK) {
            if (!reported) {
                reported = true;
                RV_LOG_WARN(scene_log_tag,
                    "the camera snap step or a snap value is outside Q20.12: the dropdown shows the default");
            }
            return RV_ERR_INVAL;
        }
        if (at) {
            index = i;
            return RV_OK;
        }
    }
    return RV_OK;
}

} // namespace

void rv_editor_scene_tools(const rv_editor_theme &theme, const rv_editor_ui_port &ui)
{
    rv_editor_scene_camera cam = ui.state.scene_camera;
    // Row 1: the tools as small tiles in a line; the tool in use is filled. The tooltip is the name, with its key.
    for (int t = 0; t < std::ssize(scene_tool_options); ++t) {
        const rv_editor_scene_tool_option &option = scene_tool_options[t];
        const rv_editor_scene_tool tool = static_cast<rv_editor_scene_tool>(t);
        const rv_editor_tile tile{ .label = option.id,
            .code = option.code,
            .color = theme.text,
            .name = rv_editor_text(option.name_id),
            .selected = cam.tool == tool };
        if (t > 0) {
            ImGui::SameLine();
        }
        if (rv_editor_tile_button(tile, rv_editor_tile_size::small, theme)) {
            cam.tool = tool;
        }
    }
    // Row 2: Snap, its step and Grid in a line.
    const char *snap_label = rv_editor_text("scene_tools.snap_label");
    rv_editor_checkbox(snap_label, &cam.snap, theme);
    rv_editor_item_tooltip(rv_editor_text("scene_tools.snap_tooltip"));
    ImGui::SameLine();
    int step = default_snap_step_index;
    // On error the dropdown keeps the default (the warning is logged).
    if (rv_editor_snap_step_index(cam, step) != RV_OK) {
        step = default_snap_step_index;
    }
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * dropdown_width_factor);
    if (rv_editor_dropdown("##snapstep", &step, snap_step_labels, std::ssize(snap_step_labels), theme)) {
        if (rv_editor_camera_pick_snap(cam, snap_step_values[step]) != RV_OK) {
            RV_LOG_WARN(scene_log_tag, "the picked snap step is outside Q20.12: the step is not changed");
        }
    }
    rv_editor_item_tooltip(rv_editor_text("scene_tools.snap_step_tooltip"));
    ImGui::SameLine();
    const char *grid_label = rv_editor_text("scene_tools.grid_label");
    rv_editor_checkbox(grid_label, &cam.grid, theme);
    const bool changed = cam != ui.state.scene_camera;
    if (changed) {
        ui.requests.push_back({ .kind = rv_editor_ui_request_kind::set_scene_camera, .camera = cam });
    }
}

} // namespace rv_editor
