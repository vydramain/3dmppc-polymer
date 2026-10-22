#pragma once

// Descriptions of a component (built-in or disc): layout and defaults as constexpr data. The editor will read it from
// disc.so's ELF section (a later step), so it holds no pointer: names and defaults are fixed arrays, and every name is
// NUL-terminated within ecs_name_capacity. Native layout on a little-endian 64-bit target (the console's). A reader
// must not compare raw bytes: padding is not zeroed.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>

#include "pdk/cv/rv_vertex.h"
#include "pdk/rv_err.h"
#include "pdkkit/component/rv_component_capacity.hpp"
#include "pdklib/rv_math/rv_fx.hpp"

namespace rv_pdkkit
{

// Field types a component may declare; each has one row in ecs_field_types.
enum class ecs_field_type : uint8_t {
    int32,
    fixed,
    fixed3,
    angle3,
    color,
    string,
    model,
};

// Items of a bracketed array "[a, b, c]".
inline constexpr std::size_t ecs_array_items = 3;

// Failure reasons of ecs_component_write_defaults; the caller prints them.
inline constexpr const char *ecs_err_count = "field count is outside the component's capacity";
inline constexpr const char *ecs_err_type = "has a type the table does not name";
inline constexpr const char *ecs_err_offset = "does not fit the destination";

// The name the editor shows, and the bytes a field takes in a component struct. A string or model takes a whole
// ecs_value_capacity, NUL included, as the scene's text does.
struct ecs_field_type_entry {
    ecs_field_type type;
    std::string_view name;
    std::size_t size;
};

inline constexpr ecs_field_type_entry ecs_field_types[] = {
    { ecs_field_type::int32, "int", sizeof(int32_t) },
    { ecs_field_type::fixed, "fixed", sizeof(rv_pdklib::fx) },
    { ecs_field_type::fixed3, "fixed3", sizeof(rv_pdklib::fx) * ecs_array_items },
    { ecs_field_type::angle3, "angle3", sizeof(rv_pdklib::fx) * ecs_array_items },
    { ecs_field_type::color, "color", sizeof(rv_color) },
    { ecs_field_type::string, "string", ecs_value_capacity },
    { ecs_field_type::model, "model", ecs_value_capacity },
};

// The table row of `type`, or nullptr when the table does not name it.
constexpr const ecs_field_type_entry *ecs_field_type_of(ecs_field_type type)
{
    for (const ecs_field_type_entry &entry : ecs_field_types) {
        if (entry.type == type) {
            return &entry;
        }
    }
    return nullptr;
}

// A field's default in the shape of its type: the member the type names; the others are unused.
union ecs_field_default {
    int32_t int32;
    rv_pdklib::fx fixed;
    rv_pdklib::fx fx3[ecs_array_items];
    rv_color color;
    char text[ecs_value_capacity];
};

// One field: name, type, byte offset inside the component struct, and default.
struct ecs_field_desc {
    char name[ecs_name_capacity];
    ecs_field_type type;
    uint32_t offset;
    ecs_field_default def;
};

// One component: name and its first field_count fields.
struct ecs_component_desc {
    char name[ecs_name_capacity];
    int64_t field_count;
    ecs_field_desc fields[ecs_component_field_capacity];
};

static_assert(sizeof(ecs_field_desc) == 136, "ecs_field_desc is the section's layout");
static_assert(sizeof(ecs_component_desc) == 1160, "ecs_component_desc is the section's layout");
static_assert(offsetof(ecs_component_desc, fields) == 72, "ecs_component_desc is the section's layout");

// Internal: the reason with the field's name in front; returns RV_ERR_INVAL. The name is read up to its NUL or the
// end of its array, whichever comes first.
inline int ecs_field_refuse(const ecs_field_desc &field, const char *reason, std::string &error)
{
    const char *end = std::find(field.name, field.name + ecs_name_capacity, '\0');
    error = "field '" + std::string(field.name, end) + "' " + reason;
    return RV_ERR_INVAL;
}

// Writes the defaults of desc into dst, a component struct of dst_size bytes, each at its field's offset. All fields
// are checked before the first write, so a refusal leaves dst untouched. Error: the reason; a field's refusal names
// the field, a count refusal does not.
inline int
ecs_component_write_defaults(const ecs_component_desc &desc, unsigned char *dst, std::size_t dst_size, std::string &error)
{
    if (desc.field_count < 0 || desc.field_count > ecs_component_field_capacity) {
        error = ecs_err_count;
        return RV_ERR_INVAL;
    }
    for (int64_t i = 0; i < desc.field_count; ++i) {
        const ecs_field_desc &field = desc.fields[i];
        const ecs_field_type_entry *entry = ecs_field_type_of(field.type);
        if (entry == nullptr) {
            return ecs_field_refuse(field, ecs_err_type, error);
        }
        if (field.offset > dst_size || entry->size > dst_size - field.offset) {
            return ecs_field_refuse(field, ecs_err_offset, error);
        }
    }
    for (int64_t i = 0; i < desc.field_count; ++i) {
        const ecs_field_desc &field = desc.fields[i];
        std::memcpy(dst + field.offset, &field.def, ecs_field_type_of(field.type)->size);
    }
    return RV_OK;
}

} // namespace rv_pdkkit
