#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <pdk/rv_err.h>

#include "pdkkit/component/rv_component_desc.hpp"
#include "pdkkit/component/rv_component_field_text.hpp"
#include "pdkkit/component/rv_component_record.hpp"
#include "pdkkit/component/rv_component_register.hpp"
#include "pdkkit/scene/rv_scene_write.hpp"
#include "pdklib/rv_math/rv_fx_text.hpp"

#include "rv_editor_scene.hpp"
#include "rv_editor_scene_fields.hpp"
#include "text/rv_editor_text.hpp"

namespace rv_editor
{

namespace
{

// Text ids of the refusal reasons (editor/texts/rv_editor_texts.toml); the where-prefix names the field or component.
// Reason of a set on a field the description does not name.
constexpr std::string_view desc_err_unnamed = "pane_scene_fields.desc_err_unnamed";
// Reason of a set on a field whose type is not in the table.
constexpr std::string_view desc_err_type = "pane_scene_fields.desc_err_type";
// Reason of a set on a field the file repeats; the extra entry is removed by remove_entry.
constexpr std::string_view desc_err_repeat = "pane_scene_fields.desc_err_repeat";
// Reason of a remove_entry with an index that is no entry of the component.
constexpr std::string_view desc_err_entry = "pane_scene_fields.desc_err_entry";

// A field's bytes, as pdkkit's parser and the factory write them; a field is at most ecs_value_capacity bytes.
using desc_bytes = std::array<unsigned char, rv_pdkkit::ecs_value_capacity>;

// The bytes of field's default, as the factory writes them into a component.
desc_bytes desc_default_bytes(const rv_pdkkit::ecs_field_desc &field, const rv_pdkkit::ecs_field_type_entry &entry)
{
    desc_bytes bytes{};
    std::memcpy(bytes.data(), &field.def, entry.size);
    return bytes;
}

// The value of a field of type from its bytes, as the file holds it, so that rv_scene_value_text writes it as the file does.
rv_pdklib::rv_manifest_mvalue desc_value_of(rv_pdkkit::ecs_field_type type, const unsigned char *bytes)
{
    rv_pdklib::rv_manifest_mvalue v;
    switch (type) {
    case rv_pdkkit::ecs_field_type::int32: {
        int32_t number = 0;
        std::memcpy(&number, bytes, sizeof(number));
        v.kind = rv_pdklib::rv_manifest_value_kind::integer;
        v.num = number;
        v.num_text = std::to_string(number);
        break;
    }
    case rv_pdkkit::ecs_field_type::fixed: {
        rv_pdklib::fx raw = 0;
        std::memcpy(&raw, bytes, sizeof(raw));
        v.kind = rv_pdklib::rv_manifest_value_kind::real;
        v.num_text = rv_pdklib::fx_to_text(raw);
        rv_editor_fields_text_value(v.num_text, v.real);
        break;
    }
    case rv_pdkkit::ecs_field_type::fixed3:
    case rv_pdkkit::ecs_field_type::angle3: {
        std::vector<std::string> texts;
        for (std::size_t i = 0; i < rv_pdkkit::ecs_array_items; ++i) {
            rv_pdklib::fx raw = 0;
            std::memcpy(&raw, bytes + i * sizeof(raw), sizeof(raw));
            texts.push_back(rv_pdklib::fx_to_text(raw));
        }
        v = rv_editor_fields_numbers_of(std::move(texts));
        break;
    }
    case rv_pdkkit::ecs_field_type::color: {
        rv_color color{};
        std::memcpy(&color, bytes, sizeof(color));
        v = rv_editor_fields_numbers_of({ std::to_string(static_cast<int>(color.r)),
            std::to_string(static_cast<int>(color.g)),
            std::to_string(static_cast<int>(color.b)) });
        break;
    }
    case rv_pdkkit::ecs_field_type::string:
    case rv_pdkkit::ecs_field_type::model: {
        // The bytes hold a NUL inside ecs_value_capacity: the parser zeroes them, and a default is zero-padded.
        v.kind = rv_pdklib::rv_manifest_value_kind::string;
        v.str = std::string(reinterpret_cast<const char *>(bytes));
        break;
    }
    }
    return v;
}

// The description's record named name, or nullptr.
const rv_pdkkit::ecs_component_desc *desc_record_find(const std::vector<rv_pdkkit::ecs_component_record> &records,
    std::string_view name)
{
    for (const rv_pdkkit::ecs_component_record &record : records) {
        if (rv_editor_scene_desc_name(record.desc) == name) {
            return &record.desc;
        }
    }
    return nullptr;
}

} // namespace

std::string_view rv_editor_scene_desc_name(const rv_pdkkit::ecs_component_desc &desc)
{
    return { desc.name, rv_pdkkit::ecs_name_length(desc.name) };
}

const rv_pdkkit::ecs_component_desc *rv_editor_scene_desc_find(std::string_view name,
    const std::vector<rv_pdkkit::ecs_component_record> *records)
{
    if (records != nullptr) {
        return desc_record_find(*records, name);
    }
    for (const rv_pdkkit::ecs_component_desc *desc : rv_pdkkit::ecs_builtin_descs()) {
        if (rv_editor_scene_desc_name(*desc) == name) {
            return desc;
        }
    }
    return nullptr;
}

std::vector<rv_editor_scene_field_row> rv_editor_scene_field_rows(const rv_editor_scene_component &c,
    const rv_pdkkit::ecs_component_desc *desc)
{
    std::vector<rv_editor_scene_field_row> rows;
    if (desc != nullptr) {
        for (int64_t i = 0; i < desc->field_count; ++i) {
            const rv_pdkkit::ecs_field_desc &field = desc->fields[i];
            const rv_pdkkit::ecs_field_type_entry *entry = rv_pdkkit::ecs_field_type_of(field.type);
            rv_editor_scene_field_row row;
            row.name = std::string(field.name, rv_pdkkit::ecs_name_length(field.name));
            row.type = entry != nullptr ? std::string(entry->name) : std::string();
            row.described = entry != nullptr;
            const rv_pdklib::rv_manifest_tree_entry *file = rv_editor_fields_find(c, row.name);
            row.is_default = file == nullptr;
            row.entry = file != nullptr ? static_cast<int>(file - c.fields.data()) : -1;
            if (file != nullptr) {
                row.text = rv_pdkkit::rv_scene_value_text(file->value);
            } else if (entry != nullptr) {
                row.text = rv_pdkkit::rv_scene_value_text(desc_value_of(field.type, desc_default_bytes(field, *entry).data()));
            }
            rows.push_back(std::move(row));
        }
    }
    for (std::size_t i = 0; i < c.fields.size(); ++i) {
        const rv_pdklib::rv_manifest_tree_entry &e = c.fields[i];
        const bool named = desc != nullptr && rv_pdkkit::ecs_desc_field_find(*desc, e.key) != nullptr;
        // The first key of a described field is the row above; a second one is shown with its note.
        if (named && rv_editor_fields_find(c, e.key) == &e) {
            continue;
        }
        rv_editor_scene_field_row row;
        row.name = e.key;
        row.text = rv_pdkkit::rv_scene_value_text(e.value);
        row.note = named ? std::string(rv_editor_text("pane_scene_fields.repeated_note")) : std::string();
        row.entry = static_cast<int>(i);
        rows.push_back(std::move(row));
    }
    return rows;
}

int rv_editor_scene_field_set(rv_editor_scene_component &c,
    const rv_pdkkit::ecs_component_desc &desc,
    std::string_view key,
    const std::string &text,
    std::string &error)
{
    const rv_pdkkit::ecs_field_desc *field = rv_pdkkit::ecs_desc_field_find(desc, key);
    if (field == nullptr) {
        error = rv_editor_fields_where(c, key) + std::string(rv_editor_text(desc_err_unnamed));
        return RV_ERR_NOENT;
    }
    const rv_pdkkit::ecs_field_type_entry *entry = rv_pdkkit::ecs_field_type_of(field->type);
    if (entry == nullptr) {
        error = rv_editor_fields_where(c, key) + std::string(rv_editor_text(desc_err_type));
        return RV_ERR_INVAL;
    }
    const auto same_key = [key](const rv_pdklib::rv_manifest_tree_entry &e) {
        return e.key == key;
    };
    if (std::count_if(c.fields.begin(), c.fields.end(), same_key) > 1) {
        error = rv_editor_fields_where(c, key) + std::string(rv_editor_text(desc_err_repeat));
        return RV_ERR_INVAL;
    }
    // The console's parser decides what reads, so the file never holds what pdkkit refuses; a refusal leaves c as it was.
    desc_bytes bytes{};
    std::string reason;
    if (rv_pdkkit::ecs_field_parse(field->type, text, bytes.data(), reason) != RV_OK) {
        error = rv_editor_fields_where(c, key) + " " + reason;
        return RV_ERR_INVAL;
    }
    if (std::memcmp(bytes.data(), desc_default_bytes(*field, *entry).data(), entry->size) == 0) {
        rv_editor_scene_field_reset(c, key);
        return RV_OK;
    }
    rv_editor_fields_put(c, key, desc_value_of(field->type, bytes.data()));
    return RV_OK;
}

int rv_editor_scene_field_remove_entry(rv_editor_scene_component &c, int index, std::string &error)
{
    const bool in_range = index >= 0 && static_cast<std::size_t>(index) < c.fields.size();
    if (!in_range) {
        error = std::string(fields_part_component) + c.name + std::string(fields_part_close) +
            std::string(rv_editor_text(desc_err_entry));
        return RV_ERR_INVAL;
    }
    c.fields.erase(c.fields.begin() + index);
    return RV_OK;
}

void rv_editor_scene_field_reset(rv_editor_scene_component &c, std::string_view key)
{
    std::erase_if(c.fields, [key](const auto &e) {
        return e.key == key;
    });
}

} // namespace rv_editor
