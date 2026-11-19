// Inspector: the selected scene object's header, its components by their descriptions and its extra keys. A name is
// committed on Enter or on leaving the field, as one undo step; a field of a component is edited in
// rv_editor_pane_scene_fields.cpp.

#include "panes/rv_editor_panes.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <string>

#include "imgui.h"

#include "panes/rv_editor_pane_tiled.hpp"
#include "text/rv_editor_text.hpp"
#include "theme/rv_editor_theme_imgui.hpp"
#include "ui/rv_editor_widgets.hpp"
#include "ui/rv_editor_widgets_content.hpp"
#include "ui/rv_editor_widgets_message.hpp"

namespace rv_editor
{

namespace
{

// Labels end this many font sizes in, as the fields' do; a longer label takes more, up to this share of the row.
constexpr float label_column_em = 6.0f;
constexpr float max_column_share = 0.5f;

// The Inspector's draft: the name's text for the object shown.
struct rv_editor_inspector_draft {
    std::string shown;
    char name[identifier_field_size] = {};
};

// The Inspector pane's own draft, shared by every Inspector tile.
rv_editor_inspector_draft inspector_draft;

// Undo step name for the name field.
constexpr const char *undo_name = "Name";

// A label column, then the field.
void rv_editor_inspector_label(const char *label, float column, const rv_editor_theme &theme)
{
    ImGui::AlignTextToFramePadding();
    rv_editor_label(label, rv_editor_label_kind::normal, theme, rv_editor_label_layout::line);
    rv_editor_inspector_field_at(column);
}

// A text value committed as a whole, never letter by letter.
void rv_editor_inspector_text(rv_editor_app &app,
    const char *label,
    std::string (*get)(const rv_editor_scene_object &),
    void (*set)(rv_editor_scene_object &, const std::string &),
    char *buf,
    size_t size,
    const std::string &id,
    const rv_editor_theme &theme,
    const char *display_label,
    float column)
{
    rv_editor_scene_doc &doc = *app.scene;
    const int at = rv_editor_scene_find(doc.scene, id);
    rv_editor_inspector_label(display_label, column, theme);
    rv_editor_inspector_field_width(buf, 0.0f);
    rv_editor_text_field((std::string("##") + label).c_str(), buf, size, theme);
    const bool commit = ImGui::IsItemDeactivatedAfterEdit();
    if (at < 0 || !commit) {
        return;
    }
    const std::string value(buf);
    if (value != get(doc.scene.objects[static_cast<size_t>(at)])) {
        rv_editor_scene_step(doc);
        set(doc.scene.objects[static_cast<size_t>(at)], value);
    }
}

} // namespace

float rv_editor_inspector_column(float widest)
{
    const float column = std::max(ImGui::GetFontSize() * label_column_em, widest + ImGui::GetStyle().ItemSpacing.x);
    return column > ImGui::GetContentRegionAvail().x * max_column_share ? 0.0f : column;
}

void rv_editor_inspector_field_width(const char *text, float reserve)
{
    // A value is shown whole: the field is as wide as its text, and the area scrolls when that is more than the room.
    const float whole = ImGui::CalcTextSize(text).x + 2.0f * ImGui::GetStyle().FramePadding.x;
    ImGui::SetNextItemWidth(std::max(ImGui::GetContentRegionAvail().x - reserve, whole));
}

void rv_editor_inspector_field_at(float column)
{
    if (column > 0.0f) {
        ImGui::SameLine(column);
    }
}

rv_editor_size rv_editor_inspector_minimum()
{
    return rv_editor_tiled_minimum(rv_editor_inspector_content_width(), rv_editor_tiled_box_block());
}

float rv_editor_inspector_content_width()
{
    return std::max(rv_editor_scene_none_width(), rv_editor_message_box_min_width({}));
}

void rv_editor_pane_scene_inspector(rv_editor_app &app, const rv_editor_theme &theme, const rv_editor_ui_port &ui)
{
    const float width = rv_editor_inspector_content_width();
    rv_editor_scene_keys(app, ui);
    const int at = app.scene != nullptr ? rv_editor_scene_find(app.scene->scene, app.scene->selected) : -1;
    // The one message box: what the pane lacks, the refusal of the last edit, or the read-only banner.
    if (app.scene == nullptr) {
        rv_editor_scene_none_box(app,
            rv_editor_text("pane_scene_inspector.no_scene_open"),
            rv_editor_label_kind::normal,
            theme,
            width);
    } else if (at < 0) {
        (void)rv_editor_message_box("##note",
            rv_editor_text("pane_scene_inspector.select_object"),
            rv_editor_label_kind::normal,
            {},
            theme,
            width);
    } else {
        const rv_editor_scene_doc &doc = *app.scene;
        const std::string refusal = rv_editor_inspector_refusal(doc, static_cast<size_t>(at));
        const std::string &ro = doc.scene.read_only;
        const std::string banner =
            ro.empty() ? std::string() : rv_editor_text_format("pane_scene_inspector.read_only", std::make_format_args(ro));
        (void)rv_editor_message_box("##note",
            refusal.empty() ? banner : refusal,
            refusal.empty() ? rv_editor_label_kind::normal : rv_editor_label_kind::error,
            {},
            theme,
            width);
    }
    (void)rv_editor_tiled_area_begin("##form_area", width, theme);
    if (app.scene == nullptr) {
        rv_editor_scene_files_list(app, theme);
    } else if (at >= 0) {
        rv_editor_scene_doc &doc = *app.scene;
        rv_editor_inspector_draft &draft = inspector_draft;
        const rv_editor_scene_object o = doc.scene.objects[static_cast<size_t>(at)];
        // The name field follows the selection and undo; the one being typed in keeps its text.
        const bool refresh = draft.shown != o.id || !ImGui::GetIO().WantTextInput;
        if (refresh) {
            std::snprintf(draft.name, sizeof(draft.name), "%s", rv_editor_object_get_name(o).c_str());
            draft.shown = o.id;
        }
        const bool read_only = !doc.scene.read_only.empty();
        const char *const name_label = rv_editor_text("pane_scene_inspector.name_label");
        const char *const kind_label = rv_editor_text("pane_scene_inspector.kind_label");
        const char *const id_label = rv_editor_text("pane_scene_inspector.id_label");
        const float column = rv_editor_inspector_column(std::max(
            { ImGui::CalcTextSize(name_label).x, ImGui::CalcTextSize(kind_label).x, ImGui::CalcTextSize(id_label).x }));
        ImGui::BeginDisabled(read_only);
        rv_editor_inspector_text(app,
            undo_name,
            rv_editor_object_get_name,
            rv_editor_object_set_name,
            draft.name,
            sizeof(draft.name),
            o.id,
            theme,
            name_label,
            column);
        rv_editor_inspector_label(kind_label, column, theme);
        const std::string kind = rv_editor_object_get_kind(o);
        const char *kind_text = kind == kind_mesh ? rv_editor_text("pane_scene_inspector.kind_mesh") : kind.c_str();
        rv_editor_label(kind_text, rv_editor_label_kind::normal, theme);
        rv_editor_inspector_label(id_label, column, theme);
        rv_editor_label(o.id.c_str(), rv_editor_label_kind::dim, theme);
        rv_editor_inspector_components(app, theme, o.id, read_only);
        if (!o.extra.empty()) {
            rv_editor_section(rv_editor_text("pane_scene_inspector.extra_header"));
            for (const auto &e : o.extra) {
                rv_editor_label(e.key.c_str(), rv_editor_label_kind::dim, theme);
            }
            rv_editor_item_tooltip(rv_editor_text("pane_scene_inspector.extra_tooltip"));
        }
        ImGui::EndDisabled();
    }
    rv_editor_tiled_area_end(theme);
}

} // namespace rv_editor
