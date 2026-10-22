// Inspector's entity type, Visible, its components with their fields, and Add component of the selected object.
// Each change is one undo step. The fields are drawn by rv_editor_pane_scene_fields.cpp.

#include "panes/rv_editor_panes.hpp"

#include <cstddef>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

#include "imgui.h"

#include "pdkkit/component/rv_component_desc.hpp"
#include "pdkkit/component/rv_component_record.hpp"
#include "pdkkit/component/rv_component_register.hpp"
#include "scene/rv_editor_scene.hpp"
#include "scene/rv_editor_scene_edit.hpp"
#include "text/rv_editor_text.hpp"
#include "ui/rv_editor_field_sizes.hpp"
#include "ui/rv_editor_widgets.hpp"

namespace rv_editor
{

namespace
{

// Labels end this many font sizes in, as the Inspector's do.
constexpr float label_column_em = 6.0f;

// Field width ImGui takes for the rest of the row.
constexpr float fill_width = -1.0f;

// A label column, then the field.
void rv_editor_components_label(const char *label, const rv_editor_theme &theme)
{
    ImGui::AlignTextToFramePadding();
    rv_editor_label(label, rv_editor_label_kind::normal, theme);
    ImGui::SameLine(ImGui::GetFontSize() * label_column_em);
    ImGui::SetNextItemWidth(fill_width);
}

// A string committed as a whole when its field is left, as one undo step. A value too long for the
// field's buffer is shown, not edited, so nothing is cut when it is written back.
void rv_editor_components_string(rv_editor_scene_doc &doc, const char *id, std::string &value, const rv_editor_theme &theme)
{
    if (value.size() >= asset_identifier_field_size) {
        rv_editor_label(value.c_str(), rv_editor_label_kind::normal, theme, rv_editor_label_layout::wrap);
        return;
    }
    char buf[asset_identifier_field_size] = {};
    std::snprintf(buf, sizeof(buf), "%s", value.c_str());
    rv_editor_text_field(id, buf, sizeof(buf), theme);
    if (ImGui::IsItemDeactivatedAfterEdit() && value != buf) {
        rv_editor_scene_step(doc);
        value = buf;
    }
}

// Visible: the checkbox is on unless the object has a hidden component; hidden carries no fields.
void rv_editor_components_visible(rv_editor_scene_doc &doc, size_t at, const rv_editor_theme &theme)
{
    bool visible = !rv_editor_scene_has_component(doc.scene.objects[at], component_hidden);
    if (!rv_editor_checkbox(rv_editor_text("pane_scene_components.visible_label"), &visible, theme)) {
        return;
    }
    rv_editor_scene_set_visible(doc, at, visible);
}

// Descriptions by pointer: the snapshot's records or the built-ins'.
using desc_list = std::vector<const rv_pdkkit::ecs_component_desc *>;

// The descriptions Add chooses from: the snapshot's when it is loaded, else the built-ins'.
desc_list rv_editor_components_descs(const rv_editor_app &app)
{
    desc_list descs;
    const std::vector<rv_pdkkit::ecs_component_record> *records = rv_editor_descs_records(app);
    if (records != nullptr) {
        for (const rv_pdkkit::ecs_component_record &record : *records) {
            descs.push_back(&record.desc);
        }
        return descs;
    }
    for (const rv_pdkkit::ecs_component_desc *desc : rv_pdkkit::ecs_builtin_descs()) {
        descs.push_back(desc);
    }
    return descs;
}

// The described components Add offers to `o`. Hidden is left out: Visible sets it. Data components are left out too:
// the Inspector edits them by their own rows, and kinds are added from the Hierarchy.
std::vector<std::string> rv_editor_components_candidates(const desc_list &descs, const rv_editor_scene_object &o)
{
    std::vector<std::string> names;
    for (const rv_pdkkit::ecs_component_desc *desc : descs) {
        const std::string_view name = rv_editor_scene_desc_name(*desc);
        if (name == component_hidden || rv_editor_scene_component_is_data(name) || rv_editor_scene_has_component(o, name)) {
            continue;
        }
        names.emplace_back(name);
    }
    return names;
}

// Add component: a button that opens the list of described components the object lacks. A pick adds the component
// with no fields, so the scene keeps only what differs from the description.
void rv_editor_components_add(const rv_editor_app &app, rv_editor_scene_doc &doc, size_t at, const rv_editor_theme &theme)
{
    const std::vector<const rv_pdkkit::ecs_component_desc *> descs = rv_editor_components_descs(app);
    if (descs.empty()) {
        rv_editor_label(rv_editor_text("pane_scene_components.add_no_descs"), rv_editor_label_kind::dim, theme);
        return;
    }
    rv_editor_scene_object &o = doc.scene.objects[at];
    const std::vector<std::string> names = rv_editor_components_candidates(descs, o);
    if (names.empty()) {
        rv_editor_label(rv_editor_text("pane_scene_components.add_none_left"), rv_editor_label_kind::dim, theme);
        return;
    }
    if (rv_editor_button(rv_editor_text("pane_scene_components.add_button"), theme)) {
        ImGui::OpenPopup("##add_component");
    }
    if (!rv_editor_popup_begin("##add_component")) {
        return;
    }
    for (const std::string &name : names) {
        if (rv_editor_menu_item(name.c_str(), nullptr, nullptr)) {
            rv_editor_scene_step(doc);
            o.components.push_back(rv_editor_scene_component{ name, {} });
        }
    }
    rv_editor_popup_end();
}

} // namespace

void rv_editor_inspector_components(rv_editor_app &app, const rv_editor_theme &theme, const std::string &id, bool read_only)
{
    rv_editor_scene_doc &doc = *app.scene;
    const int found = rv_editor_scene_find(doc.scene, id);
    // The caller passes the object it has just looked up, so this exit is not reached in practice.
    if (found < 0) {
        return;
    }
    const size_t at = static_cast<size_t>(found);
    ImGui::BeginDisabled(read_only);
    rv_editor_components_label(rv_editor_text("pane_scene_components.entity_label"), theme);
    rv_editor_components_string(doc, "##entity", doc.scene.objects[at].entity, theme);
    rv_editor_item_tooltip(rv_editor_text("pane_scene_components.entity_tooltip"));
    rv_editor_components_visible(doc, at, theme);
    rv_editor_section(rv_editor_text("pane_scene_components.components_header"));
    rv_editor_inspector_fields(app, theme, at);
    rv_editor_components_add(app, doc, at, theme);
    ImGui::EndDisabled();
}

} // namespace rv_editor
