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
#include <vector>

#include "pdkkit/scene/rv_scene.hpp"

namespace rv_pdkkit
{

namespace detail
{

// Section and key names, as rv_scene_parse reads them.
inline constexpr const char *scene_section_name = "scene";
inline constexpr const char *object_section_name = "object";
inline constexpr const char *object_id_key = "id";
inline constexpr const char *object_parent_key = "parent";
inline constexpr const char *object_entity_key = "entity";
inline constexpr const char *component_section_name = "component";
inline constexpr const char *component_object_key = "object";
inline constexpr const char *component_name_key = "name";

// Delimiters of the dialect.
inline constexpr const char *quote_mark = "\"";
inline constexpr const char *entry_sep = " = ";
inline constexpr const char *array_start = "[";
inline constexpr const char *array_end = "]";
inline constexpr const char *array_sep = ", ";
inline constexpr const char *line_end = "\n";

// Sign and the "0." of a fixed-point number below one.
inline constexpr int sign_bytes = 1;
inline constexpr int zero_point_bytes = 2;

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

// True when the source text reads back as exactly v; an in-memory edit of v makes the text stale.
inline bool rv_scene_text_reads_back(const std::string &text, double v)
{
    double back = 0.0;
    const char *end = text.data() + text.size();
    const auto r = std::from_chars(text.data(), end, back);
    return r.ec == std::errc{} && r.ptr == end && back == v;
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

// "[a, b, c]" of numbers: each element as its source text (nums_text) when that text still reads back
// to the value, else its fixed rendering.
inline std::string rv_scene_number_array(const std::vector<double> &nums, const std::vector<std::string> &texts)
{
    std::string out = array_start;
    for (size_t i = 0; i < nums.size(); ++i) {
        const bool has_text = i < texts.size() && rv_scene_text_reads_back(texts[i], nums[i]);
        out += i == 0 ? "" : array_sep;
        out += has_text ? texts[i] : rv_scene_number(nums[i]);
    }
    return out + array_end;
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

inline void rv_scene_object_text(std::string &t, const rv_scene &scene, const rv_scene_object &o)
{
    t += line_end;
    rv_scene_header(t, object_section_name, true);
    rv_scene_field(t, object_id_key, rv_scene_quote(o.id));
    rv_scene_field(t, object_parent_key, rv_scene_quote(rv_scene_parent_id(scene, o.parent)));
    if (!o.entity.empty()) {
        rv_scene_field(t, object_entity_key, rv_scene_quote(o.entity));
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
        return detail::rv_scene_text_reads_back(v.num_text, v.real) ? v.num_text : detail::rv_scene_number(v.real);
    case rv_pdklib::rv_manifest_value_kind::array:
        return detail::rv_scene_array(v.arr.data(), v.arr.size());
    case rv_pdklib::rv_manifest_value_kind::numbers:
        return detail::rv_scene_number_array(v.nums, v.nums_text);
    }
    return detail::rv_scene_quote(std::string());
}

inline std::string rv_scene_render(const rv_scene &scene)
{
    std::string t = scene.preamble;
    if (!t.empty() && t.back() != '\n') {
        t += detail::line_end;
    }
    detail::rv_scene_header(t, detail::scene_section_name, false);
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
