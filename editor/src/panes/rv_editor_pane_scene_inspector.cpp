// Inspector: the selected scene object's header, its components by their descriptions and its extra keys. A name is
// committed on Enter or on leaving the field, as one undo step; a field of a component is edited in
// rv_editor_pane_scene_fields.cpp.

#include "panes/rv_editor_panes.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <string>

#include "imgui.h"

#include "text/rv_editor_text.hpp"
#include "theme/rv_editor_theme_imgui.hpp"
#include "ui/rv_editor_widgets.hpp"

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
    ImGui::SetNextItemWidth(-1.0f);
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

void rv_editor_inspector_field_at(float column)
{
    if (column > 0.0f) {
        ImGui::SameLine(column);
    }
}

void rv_editor_pane_scene_inspector(rv_editor_app &app, const rv_editor_theme &theme, const rv_editor_ui_port &ui)
{
    if (!app.project.open) {
        rv_editor_open_project_row(app, theme);
        return;
    }
    rv_editor_scene_keys(app, ui);
    // Content only, no controls row: the whole pane is one well. Early returns
    // inside stay reachable, so the body is a lambda that always runs to well_end.
    rv_editor_well_begin("##well", ImVec2(0, 0), theme);
    const auto body = [&]() {
        if (app.scene == nullptr) {
            rv_editor_label(rv_editor_text("pane_scene_inspector.no_scene_open"),
                rv_editor_label_kind::normal,
                theme,
                rv_editor_label_layout::wrap);
            rv_editor_scene_open_row(app, theme);
            return;
        }
        rv_editor_scene_doc &doc = *app.scene;
        rv_editor_inspector_draft &draft = inspector_draft;
        const int at = rv_editor_scene_find(doc.scene, doc.selected);
        if (at < 0) {
            rv_editor_label(rv_editor_text("pane_scene_inspector.select_object"),
                rv_editor_label_kind::normal,
                theme,
                rv_editor_label_layout::wrap);
            return;
        }
        const rv_editor_scene_object o = doc.scene.objects[static_cast<size_t>(at)];
        // The name field follows the selection and undo; the one being typed in keeps its text.
        const bool refresh = draft.shown != o.id || !ImGui::GetIO().WantTextInput;
        if (refresh) {
            std::snprintf(draft.name, sizeof(draft.name), "%s", rv_editor_object_get_name(o).c_str());
            draft.shown = o.id;
        }
        const bool read_only = !doc.scene.read_only.empty();
        // The banner's place is kept when there is none, so the fields below do not move.
        std::string banner;
        if (read_only) {
            const std::string &ro = doc.scene.read_only;
            banner = rv_editor_text_format("pane_scene_inspector.read_only", std::make_format_args(ro));
        }
        rv_editor_message_line("##read_only", banner.c_str(), 2, 0.0f, theme);
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
    };
    body();
    rv_editor_well_end();
}

} // namespace rv_editor
