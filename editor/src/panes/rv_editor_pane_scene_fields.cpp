// Inspector's components by their descriptions: each field with its value or its default (dim). An edit writes only
// a difference from the default, committed on Enter or on leaving the field. A change of the file is one undo step;
// an edit, reset or remove that changes nothing makes none.

#include "panes/rv_editor_panes.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

#include "imgui.h"

#include "pdk/rv_err.h"
#include "pdkkit/component/rv_component_desc.hpp"
#include "pdkkit/component/rv_component_record.hpp"
#include "project/rv_editor_descriptions.hpp"
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

// Field width when no button sits beside it: ImGui's rest of the row.
constexpr float fill_width = -1.0f;

// The widget id of a field's value; the field's name is the label beside it.
constexpr const char *value_id = "##value";

// The description that applies to c: the snapshot's when it is loaded, else the built-ins'.
const rv_pdkkit::ecs_component_desc *rv_editor_fields_desc(const rv_editor_app &app, const rv_editor_scene_component &c)
{
    const bool loaded = app.descriptions.snapshot.state == rv_editor_descriptions_state::loaded;
    const std::vector<rv_pdkkit::ecs_component_record> *records = loaded ? &app.descriptions.snapshot.records : nullptr;
    return rv_editor_scene_desc_find(c.name, records);
}

// True when the last refused edit was of field `key` of the component at `index` of the object at `at`, and no step,
// undo or redo has happened since.
bool rv_editor_fields_refused(const rv_editor_scene_doc &doc, size_t at, size_t index, const std::string &key)
{
    const rv_editor_scene_object &o = doc.scene.objects[at];
    const rv_editor_scene_field_refusal &r = doc.field_refusal;
    return r.steps == doc.steps && r.undo == doc.undo.size() && r.object == o.id && r.component == o.components[index].name &&
        r.key == key;
}

// Two field values hold the same content; the source line they stand on does not count.
bool rv_editor_fields_value_same(const rv_pdklib::rv_manifest_mvalue &a, const rv_pdklib::rv_manifest_mvalue &b)
{
    return a.kind == b.kind && a.str == b.str && a.num == b.num && a.num_text == b.num_text && a.real == b.real &&
        a.arr == b.arr && a.nums == b.nums && a.nums_text == b.nums_text;
}

// True when c and d have the same fields with the same content, in the same order.
bool rv_editor_fields_same(const rv_editor_scene_component &c, const rv_editor_scene_component &d)
{
    return std::equal(c.fields.begin(),
        c.fields.end(),
        d.fields.begin(),
        d.fields.end(),
        [](const rv_pdklib::rv_manifest_tree_entry &e, const rv_pdklib::rv_manifest_tree_entry &f) {
            return e.key == f.key && rv_editor_fields_value_same(e.value, f.value);
        });
}

// Stores next as the component at `at` and `index`, one undo step, unless it holds the same fields already.
void rv_editor_fields_store(rv_editor_scene_doc &doc, size_t at, size_t index, rv_editor_scene_component next)
{
    doc.field_refusal = {};
    if (rv_editor_fields_same(doc.scene.objects[at].components[index], next)) {
        return;
    }
    rv_editor_scene_step(doc);
    doc.scene.objects[at].components[index] = std::move(next);
}

// Keeps the reason an edit of field `key` was refused, to show under that field.
void rv_editor_fields_refuse(rv_editor_scene_doc &doc,
    size_t at,
    size_t index,
    const std::string &key,
    const std::string &reason)
{
    const rv_editor_scene_object &o = doc.scene.objects[at];
    doc.field_refusal = { o.id, o.components[index].name, key, reason, doc.steps, doc.undo.size() };
}

// Sets field `key` of the component at `index` to `text`. A refusal leaves the file as it was.
void rv_editor_fields_commit(rv_editor_scene_doc &doc,
    size_t at,
    size_t index,
    const rv_pdkkit::ecs_component_desc &desc,
    const std::string &key,
    const std::string &text)
{
    rv_editor_scene_component next = doc.scene.objects[at].components[index];
    std::string error;
    if (rv_editor_scene_field_set(next, desc, key, text, error) != RV_OK) {
        rv_editor_fields_refuse(doc, at, index, key, error);
        return;
    }
    rv_editor_fields_store(doc, at, index, std::move(next));
}

// Removes every entry of field `key` from the component at `index`: the default applies again.
void rv_editor_fields_reset(rv_editor_scene_doc &doc, size_t at, size_t index, const std::string &key)
{
    rv_editor_scene_component next = doc.scene.objects[at].components[index];
    rv_editor_scene_field_reset(next, key);
    rv_editor_fields_store(doc, at, index, std::move(next));
}

// Removes the entry of `row` from the component at `index`, so a repeated key keeps its other entries. A refusal
// leaves the file as it was.
void rv_editor_fields_remove_entry(rv_editor_scene_doc &doc, size_t at, size_t index, const rv_editor_scene_field_row &row)
{
    rv_editor_scene_component next = doc.scene.objects[at].components[index];
    std::string error;
    if (rv_editor_scene_field_remove_entry(next, row.entry, error) != RV_OK) {
        rv_editor_fields_refuse(doc, at, index, row.name, error);
        return;
    }
    rv_editor_fields_store(doc, at, index, std::move(next));
}

// Hidden is Visible's to set and a data component is the object's own: neither is offered a Remove.
bool rv_editor_fields_removable(const std::string &name)
{
    return name != component_hidden && !rv_editor_scene_component_is_data(name);
}

// Removes the component at `index` of the object at `at`, one undo step; its fields go with it.
void rv_editor_fields_remove(rv_editor_scene_doc &doc, size_t at, size_t index)
{
    rv_editor_scene_step(doc);
    std::vector<rv_editor_scene_component> &components = doc.scene.objects[at].components;
    components.erase(components.begin() + static_cast<std::ptrdiff_t>(index));
    doc.field_refusal = {};
}

// The field's name, its type dim, then the column where its value starts.
void rv_editor_fields_label(const rv_editor_scene_field_row &row, const rv_editor_theme &theme)
{
    ImGui::AlignTextToFramePadding();
    rv_editor_label(row.name.c_str(), rv_editor_label_kind::normal, theme);
    if (!row.type.empty()) {
        ImGui::SameLine();
        rv_editor_label(row.type.c_str(), rv_editor_label_kind::dim, theme);
    }
    ImGui::SameLine(ImGui::GetFontSize() * label_column_em);
}

// A described field's value: an input, dim while it is the default, and a Reset beside it once the file sets it.
void rv_editor_fields_edit(rv_editor_scene_doc &doc,
    size_t at,
    size_t index,
    const rv_pdkkit::ecs_component_desc &desc,
    const rv_editor_scene_field_row &row,
    const rv_editor_theme &theme)
{
    const char *reset = rv_editor_text("pane_scene_fields.reset_button");
    const bool can_reset = !row.is_default;
    const float width = can_reset ? -(rv_editor_button_width(reset) + ImGui::GetStyle().ItemSpacing.x) : fill_width;
    char buf[asset_identifier_field_size] = {};
    std::snprintf(buf, sizeof(buf), "%s", row.text.c_str());
    const bool fits = row.text.size() < sizeof(buf);
    const bool dimmed = fits && row.is_default;
    if (dimmed) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    }
    if (fits) {
        ImGui::SetNextItemWidth(width);
        rv_editor_text_field(value_id, buf, sizeof(buf), theme);
        if (ImGui::IsItemDeactivatedAfterEdit()) {
            rv_editor_fields_commit(doc, at, index, desc, row.name, buf);
        }
    } else {
        rv_editor_label(row.text.c_str(), rv_editor_label_kind::normal, theme, rv_editor_label_layout::wrap);
    }
    if (dimmed) {
        ImGui::PopStyleColor();
    }
    if (can_reset) {
        ImGui::SameLine();
        if (rv_editor_button(reset, theme)) {
            rv_editor_fields_reset(doc, at, index, row.name);
        }
    }
}

// One field row. A described field is edited; an unknown field, a repeated key and every field of an undescribed
// component are read-only text, and an unknown field has a Remove. A refusal of the row shows under it.
void rv_editor_fields_row(rv_editor_scene_doc &doc,
    size_t at,
    size_t index,
    const rv_pdkkit::ecs_component_desc *desc,
    const rv_editor_scene_field_row &row,
    const rv_editor_theme &theme)
{
    rv_editor_fields_label(row, theme);
    if (desc != nullptr && row.described && row.note.empty()) {
        rv_editor_fields_edit(doc, at, index, *desc, row, theme);
    } else {
        rv_editor_label(row.text.c_str(), rv_editor_label_kind::normal, theme, rv_editor_label_layout::wrap);
        if (!row.note.empty()) {
            rv_editor_label(row.note.c_str(), rv_editor_label_kind::dim, theme);
        }
        const bool removable = desc != nullptr && row.entry >= 0;
        if (removable && rv_editor_button(rv_editor_text("pane_scene_fields.remove_button"), theme)) {
            rv_editor_fields_remove_entry(doc, at, index, row);
        }
    }
    if (rv_editor_fields_refused(doc, at, index, row.name)) {
        rv_editor_label(doc.field_refusal.reason.c_str(), rv_editor_label_kind::error, theme, rv_editor_label_layout::wrap);
    }
}

} // namespace

void rv_editor_inspector_fields(rv_editor_app &app, const rv_editor_theme &theme, size_t at)
{
    rv_editor_scene_doc &doc = *app.scene;
    const size_t count = doc.scene.objects[at].components.size();
    // Removal waits until the walk is over: it changes the list being walked.
    size_t removing = count;
    for (size_t c = 0; c < count; ++c) {
        const rv_editor_scene_component &comp = doc.scene.objects[at].components[c];
        const rv_pdkkit::ecs_component_desc *desc = rv_editor_fields_desc(app, comp);
        const std::vector<rv_editor_scene_field_row> rows = rv_editor_scene_field_rows(comp, desc);
        ImGui::PushID(static_cast<int>(c));
        rv_editor_section(comp.name.c_str());
        const bool removable = rv_editor_fields_removable(comp.name);
        if (removable && rv_editor_button(rv_editor_text("pane_scene_fields.remove_component_button"), theme)) {
            removing = c;
        }
        if (desc == nullptr) {
            rv_editor_label(rv_editor_text("pane_scene_fields.no_description"), rv_editor_label_kind::dim, theme);
        }
        for (size_t r = 0; r < rows.size(); ++r) {
            ImGui::PushID(static_cast<int>(r));
            rv_editor_fields_row(doc, at, c, desc, rows[r], theme);
            ImGui::PopID();
        }
        ImGui::PopID();
    }
    if (removing < count) {
        rv_editor_fields_remove(doc, at, removing);
    }
}

} // namespace rv_editor
