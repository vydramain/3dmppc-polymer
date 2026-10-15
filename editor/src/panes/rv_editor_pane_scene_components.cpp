// Inspector's entity type, Visible and components of the selected object. Each change is one undo step.
// A field is edited by its value's kind; arrays and numbers lists are shown as text.

#include "panes/rv_editor_panes.hpp"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

#include "imgui.h"

#include "pdklib/rv_scene/rv_scene.hpp"
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

// Real fields show six decimals; the file keeps the value in full, and a field left unedited writes nothing.
constexpr const char *real_format = "%.6f";

// Real fields have no arrow or drag steps.
constexpr double no_step = 0.0;

// Field width ImGui takes for the rest of the row.
constexpr float fill_width = -1.0f;

// Blanks that do not count in a component name.
constexpr std::string_view blank_chars = " \t";

// The widget id of a field's value; the field's key is the label beside it.
constexpr const char *value_id = "##value";

// The Add component name being typed, for the object it was typed for.
struct rv_editor_add_draft {
    std::string shown;
    char name[identifier_field_size] = {};
};

rv_editor_add_draft add_draft;

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

// An integer committed when its field is left, as one undo step.
void rv_editor_components_integer(rv_editor_scene_doc &doc, int64_t &value)
{
    int64_t shown = value;
    ImGui::InputScalar(value_id, ImGuiDataType_S64, &shown);
    if (ImGui::IsItemDeactivatedAfterEdit() && shown != value) {
        rv_editor_scene_step(doc);
        value = shown;
    }
}

// A real committed when its field is left, as one undo step.
void rv_editor_components_real(rv_editor_scene_doc &doc, double &value)
{
    double shown = value;
    ImGui::InputDouble(value_id, &shown, no_step, no_step, real_format);
    if (ImGui::IsItemDeactivatedAfterEdit() && shown != value) {
        rv_editor_scene_step(doc);
        value = shown;
    }
}

// One field of a component: its key, then the value as its kind is edited.
void rv_editor_components_field(rv_editor_scene_doc &doc,
    rv_pdklib::rv_manifest_tree_entry &field,
    const rv_editor_theme &theme)
{
    rv_editor_components_label(field.key.c_str(), theme);
    rv_pdklib::rv_manifest_mvalue &v = field.value;
    switch (v.kind) {
    case rv_pdklib::rv_manifest_value_kind::string:
        rv_editor_components_string(doc, value_id, v.str, theme);
        return;
    case rv_pdklib::rv_manifest_value_kind::integer:
        rv_editor_components_integer(doc, v.num);
        return;
    case rv_pdklib::rv_manifest_value_kind::real:
        rv_editor_components_real(doc, v.real);
        return;
    case rv_pdklib::rv_manifest_value_kind::array:
    case rv_pdklib::rv_manifest_value_kind::numbers:
        rv_editor_label(rv_pdklib::rv_scene_value_text(v).c_str(), rv_editor_label_kind::normal, theme);
        return;
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

// The components but hidden: each with a header, a Remove button and its fields.
void rv_editor_components_list(rv_editor_scene_doc &doc, size_t at, const rv_editor_theme &theme)
{
    rv_editor_scene_object &o = doc.scene.objects[at];
    // Removal waits until the walk is over: it changes the list being walked.
    size_t removing = o.components.size();
    for (size_t c = 0; c < o.components.size(); ++c) {
        rv_editor_scene_component &comp = o.components[c];
        if (comp.name == component_hidden) {
            continue;
        }
        ImGui::PushID(static_cast<int>(c));
        rv_editor_section(comp.name.c_str());
        if (rv_editor_button(rv_editor_text("pane_scene_components.remove_button"), theme)) {
            removing = c;
        }
        for (size_t f = 0; f < comp.fields.size(); ++f) {
            ImGui::PushID(static_cast<int>(f));
            rv_editor_components_field(doc, comp.fields[f], theme);
            ImGui::PopID();
        }
        ImGui::PopID();
    }
    if (removing == o.components.size()) {
        return;
    }
    rv_editor_scene_step(doc);
    o.components.erase(o.components.begin() + static_cast<std::ptrdiff_t>(removing));
}

// The name without blanks at either end; all blanks leave an empty name.
std::string_view rv_editor_components_trim(const char *name)
{
    const std::string_view view(name);
    const size_t first = view.find_first_not_of(blank_chars);
    if (first == std::string_view::npos) {
        return {};
    }
    const size_t last = view.find_last_not_of(blank_chars);
    return view.substr(first, last - first + 1);
}

// Why `name` cannot be added to `o` as a component, or nullptr when it can.
const char *rv_editor_components_add_reason(const rv_editor_scene_object &o, std::string_view name)
{
    if (name.empty()) {
        return rv_editor_text("pane_scene_components.add_empty");
    }
    if (name == component_hidden) {
        return rv_editor_text("pane_scene_components.add_hidden");
    }
    if (rv_editor_scene_has_component(o, name)) {
        return rv_editor_text("pane_scene_components.add_exists");
    }
    return nullptr;
}

// Add component: a name and a button; the new component has no fields, so the scene keeps only what differs.
void rv_editor_components_add(rv_editor_scene_doc &doc, size_t at, const rv_editor_theme &theme)
{
    rv_editor_scene_object &o = doc.scene.objects[at];
    rv_editor_components_label(rv_editor_text("pane_scene_components.add_name_label"), theme);
    rv_editor_text_field("##add_name", add_draft.name, sizeof(add_draft.name), theme);
    const std::string_view name = rv_editor_components_trim(add_draft.name);
    const char *reason = rv_editor_components_add_reason(o, name);
    const bool clicked = rv_editor_button(rv_editor_text("pane_scene_components.add_button"), theme, { .disabled = reason });
    if (reason != nullptr) {
        return;
    }
    if (!clicked) {
        return;
    }
    rv_editor_scene_step(doc);
    o.components.push_back(rv_editor_scene_component{ std::string(name), {} });
    add_draft.name[0] = '\0';
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
    // The name being typed belongs to the object it was typed for.
    if (add_draft.shown != id) {
        add_draft.shown = id;
        add_draft.name[0] = '\0';
    }
    ImGui::BeginDisabled(read_only);
    rv_editor_components_label(rv_editor_text("pane_scene_components.entity_label"), theme);
    rv_editor_components_string(doc, "##entity", doc.scene.objects[at].entity, theme);
    rv_editor_item_tooltip(rv_editor_text("pane_scene_components.entity_tooltip"));
    rv_editor_components_visible(doc, at, theme);
    rv_editor_section(rv_editor_text("pane_scene_components.components_header"));
    rv_editor_components_list(doc, at, theme);
    rv_editor_components_add(doc, at, theme);
    ImGui::EndDisabled();
}

} // namespace rv_editor
