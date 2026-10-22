#pragma once

// The scene writer: a scene back as disc.toml's dialect, the inverse of rv_scene_parse.
// Included by rv_scene.hpp after rv_scene_read.hpp.
// Inline: a disc gets the SDK as headers only, with no object code.

#include <algorithm>
#include <array>
#include <charconv>
#include <concepts>
#include <cstdint>
#include <iterator>
#include <limits>
#include <string>
#include <system_error>

#include "pdk/de/rv_dv.h"
#include "pdkkit/scene/rv_scene.hpp"

namespace rv_pdkkit
{

namespace detail
{

// Section and key names, as rv_scene_parse reads them.
inline constexpr const char *scene_section_name = "scene";
inline constexpr const char *scene_version_key = "version";
inline constexpr const char *object_section_name = "object";
inline constexpr const char *object_id_key = "id";
inline constexpr const char *object_name_key = "name";
inline constexpr const char *object_parent_key = "parent";
inline constexpr const char *object_kind_key = "kind";
inline constexpr const char *object_entity_key = "entity";
inline constexpr const char *object_position_key = "position";
inline constexpr const char *object_rotation_key = "rotation";
inline constexpr const char *object_scale_key = "scale";
inline constexpr const char *object_mesh_key = "mesh";
inline constexpr const char *object_texture_key = "texture";
inline constexpr const char *object_uv_key = "uv";
inline constexpr const char *object_tint_key = "tint";
inline constexpr const char *object_tess_key = "tess";
inline constexpr const char *component_section_name = "component";
inline constexpr const char *component_object_key = "object";
inline constexpr const char *component_name_key = "name";
inline constexpr const char *kind_quad = "quad";
inline constexpr const char *kind_billboard = "billboard";

// Delimiters of the dialect.
inline constexpr const char *quote_mark = "\"";
inline constexpr const char *entry_sep = " = ";
inline constexpr const char *array_start = "[";
inline constexpr const char *array_end = "]";
inline constexpr const char *array_sep = ", ";
inline constexpr const char *line_end = "\n";

// Element counts of the fixed-size values.
inline constexpr size_t vec3_size = 3;
inline constexpr size_t color_channels = 3;

// Sign and the "0." of a fixed-point number below one.
inline constexpr int sign_bytes = 1;
inline constexpr int zero_point_bytes = 2;

// Room for the longest fixed text of a Q20.12 value: the sign, the six digits of 524288, the point
// and fx_shift decimals.
inline constexpr int unit_integer_digits = 6;
inline constexpr size_t unit_buffer_bytes = static_cast<size_t>(sign_bytes + unit_integer_digits + 1 + rv_pdklib::fx_shift);

// Room for the longest fixed text of T: the digits of the smallest subnormal after "0.",
// or the digits of the largest magnitude. Covers every value, so to_chars never fails.
template <std::floating_point T>
inline constexpr size_t number_buffer_bytes = static_cast<size_t>(
    std::max(sign_bytes + zero_point_bytes + (-std::numeric_limits<T>::min_exponent10 + std::numeric_limits<T>::max_digits10),
        sign_bytes + (std::numeric_limits<T>::max_exponent10 + 1)));

// The escapes the dialect reads back; any other byte is written as it is.
struct escape_rule {
    char from;
    const char *to;
};
inline constexpr std::array<escape_rule, 5> escape_rules = { {
    { '"', "\\\"" },
    { '\\', "\\\\" },
    { '\n', "\\n" },
    { '\t', "\\t" },
    { '\r', "\\r" },
} };

// "[name]" for a table, "[[name]]" for one table of an array.
struct header_brackets_pair {
    const char *open;
    const char *close;
};
inline constexpr std::array<header_brackets_pair, 2> header_brackets = { {
    { "[", "]" },
    { "[[", "]]" },
} };

inline std::string rv_scene_quote(const std::string &s)
{
    std::string out = quote_mark;
    for (const char c : s) {
        const auto rule = std::find_if(escape_rules.begin(), escape_rules.end(), [c](const escape_rule &r) {
            return r.from == c;
        });
        out += rule == escape_rules.end() ? std::string(1, c) : std::string(rule->to);
    }
    return out + quote_mark;
}

// Shortest text that reads back as the same value, in fixed form: the dialect has no exponent.
template <std::floating_point T>
inline std::string rv_scene_number(T v)
{
    std::array<char, number_buffer_bytes<T>> buf{};
    const auto r = std::to_chars(buf.data(), buf.data() + buf.size(), v, std::chars_format::fixed);
    return std::string(buf.data(), r.ptr);
}

inline std::string rv_scene_element(double v)
{
    return rv_scene_number(v);
}

inline std::string rv_scene_element(int v)
{
    return std::to_string(v);
}

inline std::string rv_scene_element(const std::string &v)
{
    return rv_scene_quote(v);
}

// "[a, b, c]" of items, each written by rv_scene_element.
template <typename T>
inline std::string rv_scene_array(const T *items, size_t count)
{
    std::string out = array_start;
    for (size_t i = 0; i < count; ++i) {
        out += i == 0 ? "" : array_sep;
        out += rv_scene_element(items[i]);
    }
    return out + array_end;
}

// Shortest fixed decimal text of a Q20.12 value that reads back, rounded as rv_scene_parse
// rounds, as the same value; fx_shift digits are always exact, as the step is 1/4096.
inline std::string rv_scene_units(rv_pdklib::fx v)
{
    std::array<char, unit_buffer_bytes> buf{};
    const double units = static_cast<double>(v) / rv_pdklib::fx_one; // exact: a double holds every fx
    for (int digits = 0;; ++digits) {
        const auto r = std::to_chars(buf.data(), buf.data() + buf.size(), units, std::chars_format::fixed, digits);
        double back = 0.0;
        const bool read = std::from_chars(buf.data(), r.ptr, back).ec == std::errc{};
        rv_pdklib::fx q = 0;
        if (digits == rv_pdklib::fx_shift || (read && rv_pdklib::fx_from_double(back, q) == RV_OK && q == v)) {
            return std::string(buf.data(), r.ptr);
        }
    }
}

// "[a, b, c]" of Q20.12 values, each written by rv_scene_units.
inline std::string rv_scene_unit_array(const rv_pdklib::fx *items, size_t count)
{
    std::string out = array_start;
    for (size_t i = 0; i < count; ++i) {
        out += i == 0 ? "" : array_sep;
        out += rv_scene_units(items[i]);
    }
    return out + array_end;
}

inline std::string rv_scene_vec3(const rv_pdklib::fx_vec3 &v)
{
    const rv_pdklib::fx parts[vec3_size] = { v.x, v.y, v.z };
    return rv_scene_unit_array(parts, vec3_size);
}

inline std::string rv_scene_tint_text(const rv_color &c)
{
    const int parts[color_channels] = { c.r, c.g, c.b };
    return rv_scene_array(parts, color_channels);
}

// One `key = value` line.
inline void rv_scene_field(std::string &t, const std::string &key, const std::string &value)
{
    t += key + entry_sep + value + line_end;
}

inline void rv_scene_entries(std::string &t, const std::vector<rv_pdklib::rv_manifest_tree_entry> &entries)
{
    for (const rv_pdklib::rv_manifest_tree_entry &e : entries) {
        rv_scene_field(t, e.key, rv_scene_value_text(e.value));
    }
}

// "[name]" or "[[name]]", then the newline.
inline void rv_scene_header(std::string &t, const std::string &name, bool array)
{
    const header_brackets_pair &b = header_brackets[static_cast<size_t>(array)];
    t += b.open + name + b.close + line_end;
}

// The parent's id, "" at the root. An index outside objects is written as the root.
inline std::string rv_scene_parent_id(const rv_scene &scene, int32_t parent)
{
    const bool root = parent < 0 || static_cast<size_t>(parent) >= scene.objects.size();
    return root ? std::string() : scene.objects[static_cast<size_t>(parent)].id;
}

// uv/tint/tess belong to quad and billboard: they are always written there. Other kinds
// keep them only when set away from the default, so a read back never drops them.
inline bool rv_scene_textured(const rv_scene_object &o)
{
    return o.kind == kind_quad || o.kind == kind_billboard;
}

inline bool rv_scene_writes_uv(const rv_scene_object &o)
{
    const rv_scene_object defaults{};
    return rv_scene_textured(o) || !std::equal(std::begin(o.uv), std::end(o.uv), std::begin(defaults.uv));
}

inline bool rv_scene_writes_tint(const rv_scene_object &o)
{
    const rv_scene_object defaults{};
    return rv_scene_textured(o) || o.tint.r != defaults.tint.r || o.tint.g != defaults.tint.g || o.tint.b != defaults.tint.b;
}

inline bool rv_scene_writes_tess(const rv_scene_object &o)
{
    const rv_scene_object defaults{};
    return rv_scene_textured(o) || o.tess != defaults.tess;
}

inline void rv_scene_object_text(std::string &t, const rv_scene &scene, const rv_scene_object &o)
{
    t += line_end;
    rv_scene_header(t, object_section_name, true);
    rv_scene_field(t, object_id_key, rv_scene_quote(o.id));
    rv_scene_field(t, object_name_key, rv_scene_quote(o.name));
    rv_scene_field(t, object_parent_key, rv_scene_quote(rv_scene_parent_id(scene, o.parent)));
    rv_scene_field(t, object_kind_key, rv_scene_quote(o.kind));
    if (!o.entity.empty()) {
        rv_scene_field(t, object_entity_key, rv_scene_quote(o.entity));
    }
    rv_scene_field(t, object_position_key, rv_scene_vec3(o.position));
    rv_scene_field(t, object_rotation_key, rv_scene_vec3(o.rotation));
    rv_scene_field(t, object_scale_key, rv_scene_vec3(o.scale));
    rv_scene_field(t, object_mesh_key, rv_scene_quote(o.mesh));
    rv_scene_field(t, object_texture_key, rv_scene_quote(o.texture));
    if (rv_scene_writes_uv(o)) {
        rv_scene_field(t, object_uv_key, rv_scene_unit_array(o.uv, std::size(o.uv)));
    }
    if (rv_scene_writes_tint(o)) {
        rv_scene_field(t, object_tint_key, rv_scene_tint_text(o.tint));
    }
    if (rv_scene_writes_tess(o)) {
        rv_scene_field(t, object_tess_key, rv_scene_units(o.tess));
    }
    rv_scene_entries(t, o.extra);
}

// The component's block, written after its object's.
inline void rv_scene_component_text(std::string &t, const std::string &object_id, const rv_scene_component &c)
{
    t += line_end;
    rv_scene_header(t, component_section_name, true);
    rv_scene_field(t, component_object_key, rv_scene_quote(object_id));
    rv_scene_field(t, component_name_key, rv_scene_quote(c.name));
    rv_scene_entries(t, c.fields);
}

} // namespace detail

inline std::string rv_scene_value_text(const rv_pdklib::rv_manifest_mvalue &v)
{
    switch (v.kind) {
    case rv_pdklib::rv_manifest_value_kind::string:
        return detail::rv_scene_quote(v.str);
    case rv_pdklib::rv_manifest_value_kind::integer:
        return std::to_string(v.num);
    case rv_pdklib::rv_manifest_value_kind::real:
        return detail::rv_scene_number(v.real);
    case rv_pdklib::rv_manifest_value_kind::array:
        return detail::rv_scene_array(v.arr.data(), v.arr.size());
    case rv_pdklib::rv_manifest_value_kind::numbers:
        return detail::rv_scene_array(v.nums.data(), v.nums.size());
    }
    return detail::rv_scene_quote(std::string());
}

inline std::string rv_scene_render(const rv_scene &scene)
{
    std::string t = scene.preamble;
    if (!t.empty() && t.back() != '\n') {
        t += detail::line_end;
    }
    // A document without a version (0.0, the default) is written as the current PDK version.
    const bool legacy = scene.version_major == 0 && scene.version_minor == 0;
    const uint32_t major = legacy ? static_cast<uint32_t>(RV_MPPC_VER_MAJOR) : scene.version_major;
    const uint32_t minor = legacy ? static_cast<uint32_t>(RV_MPPC_VER_MINOR) : scene.version_minor;
    detail::rv_scene_header(t, detail::scene_section_name, false);
    detail::rv_scene_field(t,
        detail::scene_version_key,
        detail::rv_scene_quote(std::to_string(major) + "." + std::to_string(minor)));
    detail::rv_scene_entries(t, scene.scene_extra);
    for (size_t i = 0; i < scene.objects.size(); ++i) {
        detail::rv_scene_object_text(t, scene, scene.objects[i]);
        for (const rv_scene_component &c : scene.components) {
            if (c.object == static_cast<int32_t>(i)) {
                detail::rv_scene_component_text(t, scene.objects[i].id, c);
            }
        }
    }
    for (const rv_pdklib::rv_manifest_tree_section &s : scene.other_sections) {
        t += detail::line_end;
        detail::rv_scene_header(t, s.name, s.array);
        detail::rv_scene_entries(t, s.entries);
    }
    return t;
}

} // namespace rv_pdkkit
