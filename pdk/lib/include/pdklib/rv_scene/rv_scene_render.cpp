// The scene writer: a scene back as disc.toml's dialect, the inverse of rv_scene_parse.

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
#include "rv_scene.hpp"

namespace rv_pdklib
{

namespace
{

// Section and key names, as rv_scene_parse reads them.
constexpr const char *scene_section_name = "scene";
constexpr const char *scene_version_key = "version";
constexpr const char *object_section_name = "object";
constexpr const char *object_id_key = "id";
constexpr const char *object_name_key = "name";
constexpr const char *object_parent_key = "parent";
constexpr const char *object_kind_key = "kind";
constexpr const char *object_entity_key = "entity";
constexpr const char *object_position_key = "position";
constexpr const char *object_rotation_key = "rotation";
constexpr const char *object_scale_key = "scale";
constexpr const char *object_mesh_key = "mesh";
constexpr const char *object_texture_key = "texture";
constexpr const char *object_uv_key = "uv";
constexpr const char *object_tint_key = "tint";
constexpr const char *object_tess_key = "tess";
constexpr const char *component_section_name = "component";
constexpr const char *component_object_key = "object";
constexpr const char *component_name_key = "name";
constexpr const char *kind_quad = "quad";
constexpr const char *kind_billboard = "billboard";

// Delimiters of the dialect.
constexpr const char *quote_mark = "\"";
constexpr const char *entry_sep = " = ";
constexpr const char *array_start = "[";
constexpr const char *array_end = "]";
constexpr const char *array_sep = ", ";
constexpr const char *line_end = "\n";

// Element counts of the fixed-size values.
constexpr size_t vec3_size = 3;
constexpr size_t color_channels = 3;

// Sign and the "0." of a fixed-point number below one.
constexpr int sign_bytes = 1;
constexpr int zero_point_bytes = 2;

// Room for the longest fixed text of a Q20.12 value: the sign, the six digits of 524288, the point
// and fx_shift decimals.
constexpr int unit_integer_digits = 6;
constexpr size_t unit_buffer_bytes = static_cast<size_t>(sign_bytes + unit_integer_digits + 1 + fx_shift);

// Room for the longest fixed text of T: the digits of the smallest subnormal after "0.",
// or the digits of the largest magnitude. Covers every value, so to_chars never fails.
template <std::floating_point T>
constexpr size_t number_buffer_bytes = static_cast<size_t>(
    std::max(sign_bytes + zero_point_bytes + (-std::numeric_limits<T>::min_exponent10 + std::numeric_limits<T>::max_digits10),
        sign_bytes + (std::numeric_limits<T>::max_exponent10 + 1)));

// The escapes the dialect reads back; any other byte is written as it is.
struct escape_rule {
    char from;
    const char *to;
};
constexpr std::array<escape_rule, 5> escape_rules = { {
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
constexpr std::array<header_brackets_pair, 2> header_brackets = { {
    { "[", "]" },
    { "[[", "]]" },
} };

std::string rv_scene_quote(const std::string &s)
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
std::string rv_scene_number(T v)
{
    std::array<char, number_buffer_bytes<T>> buf{};
    const auto r = std::to_chars(buf.data(), buf.data() + buf.size(), v, std::chars_format::fixed);
    return std::string(buf.data(), r.ptr);
}

std::string rv_scene_element(double v)
{
    return rv_scene_number(v);
}

std::string rv_scene_element(int v)
{
    return std::to_string(v);
}

std::string rv_scene_element(const std::string &v)
{
    return rv_scene_quote(v);
}

// "[a, b, c]" of items, each written by rv_scene_element.
template <typename T>
std::string rv_scene_array(const T *items, size_t count)
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
std::string rv_scene_units(fx v)
{
    std::array<char, unit_buffer_bytes> buf{};
    const double units = static_cast<double>(v) / fx_one; // exact: a double holds every fx
    for (int digits = 0;; ++digits) {
        const auto r = std::to_chars(buf.data(), buf.data() + buf.size(), units, std::chars_format::fixed, digits);
        double back = 0.0;
        const bool read = std::from_chars(buf.data(), r.ptr, back).ec == std::errc{};
        fx q = 0;
        if (digits == fx_shift || (read && fx_from_double(back, q) == RV_OK && q == v)) {
            return std::string(buf.data(), r.ptr);
        }
    }
}

// "[a, b, c]" of Q20.12 values, each written by rv_scene_units.
std::string rv_scene_unit_array(const fx *items, size_t count)
{
    std::string out = array_start;
    for (size_t i = 0; i < count; ++i) {
        out += i == 0 ? "" : array_sep;
        out += rv_scene_units(items[i]);
    }
    return out + array_end;
}

std::string rv_scene_vec3(const fx_vec3 &v)
{
    const fx parts[vec3_size] = { v.x, v.y, v.z };
    return rv_scene_unit_array(parts, vec3_size);
}

std::string rv_scene_tint(const rv_color &c)
{
    const int parts[color_channels] = { c.r, c.g, c.b };
    return rv_scene_array(parts, color_channels);
}

// One `key = value` line.
void rv_scene_field(std::string &t, const std::string &key, const std::string &value)
{
    t += key + entry_sep + value + line_end;
}

void rv_scene_entries(std::string &t, const std::vector<rv_manifest_tree_entry> &entries)
{
    for (const rv_manifest_tree_entry &e : entries) {
        rv_scene_field(t, e.key, rv_scene_value_text(e.value));
    }
}

// "[name]" or "[[name]]", then the newline.
void rv_scene_header(std::string &t, const std::string &name, bool array)
{
    const header_brackets_pair &b = header_brackets[static_cast<size_t>(array)];
    t += b.open + name + b.close + line_end;
}

// The parent's id, "" at the root. An index outside objects is written as the root.
std::string rv_scene_parent_id(const rv_scene &scene, int32_t parent)
{
    const bool root = parent < 0 || static_cast<size_t>(parent) >= scene.objects.size();
    return root ? std::string() : scene.objects[static_cast<size_t>(parent)].id;
}

// uv/tint/tess belong to quad and billboard: they are always written there. Other kinds
// keep them only when set away from the default, so a read back never drops them.
bool rv_scene_textured(const rv_scene_object &o)
{
    return o.kind == kind_quad || o.kind == kind_billboard;
}

bool rv_scene_writes_uv(const rv_scene_object &o)
{
    const rv_scene_object defaults{};
    return rv_scene_textured(o) || !std::equal(std::begin(o.uv), std::end(o.uv), std::begin(defaults.uv));
}

bool rv_scene_writes_tint(const rv_scene_object &o)
{
    const rv_scene_object defaults{};
    return rv_scene_textured(o) || o.tint.r != defaults.tint.r || o.tint.g != defaults.tint.g || o.tint.b != defaults.tint.b;
}

bool rv_scene_writes_tess(const rv_scene_object &o)
{
    const rv_scene_object defaults{};
    return rv_scene_textured(o) || o.tess != defaults.tess;
}

void rv_scene_object_text(std::string &t, const rv_scene &scene, const rv_scene_object &o)
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
        rv_scene_field(t, object_tint_key, rv_scene_tint(o.tint));
    }
    if (rv_scene_writes_tess(o)) {
        rv_scene_field(t, object_tess_key, rv_scene_units(o.tess));
    }
    rv_scene_entries(t, o.extra);
}

// The component's block, written after its object's.
void rv_scene_component_text(std::string &t, const std::string &object_id, const rv_scene_component &c)
{
    t += line_end;
    rv_scene_header(t, component_section_name, true);
    rv_scene_field(t, component_object_key, rv_scene_quote(object_id));
    rv_scene_field(t, component_name_key, rv_scene_quote(c.name));
    rv_scene_entries(t, c.fields);
}

} // namespace

std::string rv_scene_value_text(const rv_manifest_mvalue &v)
{
    switch (v.kind) {
    case rv_manifest_value_kind::string:
        return rv_scene_quote(v.str);
    case rv_manifest_value_kind::integer:
        return std::to_string(v.num);
    case rv_manifest_value_kind::real:
        return rv_scene_number(v.real);
    case rv_manifest_value_kind::array:
        return rv_scene_array(v.arr.data(), v.arr.size());
    case rv_manifest_value_kind::numbers:
        return rv_scene_array(v.nums.data(), v.nums.size());
    }
    return rv_scene_quote(std::string());
}

std::string rv_scene_render(const rv_scene &scene)
{
    std::string t = scene.preamble;
    if (!t.empty() && t.back() != '\n') {
        t += line_end;
    }
    // A document without a version (0.0, the default) is written as the current PDK version.
    const bool legacy = scene.version_major == 0 && scene.version_minor == 0;
    const uint32_t major = legacy ? static_cast<uint32_t>(RV_MPPC_VER_MAJOR) : scene.version_major;
    const uint32_t minor = legacy ? static_cast<uint32_t>(RV_MPPC_VER_MINOR) : scene.version_minor;
    rv_scene_header(t, scene_section_name, false);
    rv_scene_field(t, scene_version_key, rv_scene_quote(std::to_string(major) + "." + std::to_string(minor)));
    rv_scene_entries(t, scene.scene_extra);
    for (size_t i = 0; i < scene.objects.size(); ++i) {
        rv_scene_object_text(t, scene, scene.objects[i]);
        for (const rv_scene_component &c : scene.components) {
            if (c.object == static_cast<int32_t>(i)) {
                rv_scene_component_text(t, scene.objects[i].id, c);
            }
        }
    }
    for (const rv_manifest_tree_section &s : scene.other_sections) {
        t += line_end;
        rv_scene_header(t, s.name, s.array);
        rv_scene_entries(t, s.entries);
    }
    return t;
}

} // namespace rv_pdklib
