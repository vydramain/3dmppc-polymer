#pragma once

// Fields of an editor scene component, as pdkkit's document holds them. Q20.12 numbers travel as decimal
// text through pdklib's fx_from_text and fx_to_text. A number's double mirror is std::from_chars of that text.

#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include <pdk/rv_err.h>

#include "pdklib/rv_manifest/detail/rv_manifest_tree.hpp"
#include "pdklib/rv_math/rv_fx.hpp"
#include "pdklib/rv_math/rv_fx_text.hpp"

#include "rv_editor_scene.hpp"

namespace rv_editor
{

// Reason parts of the errors; the field's name comes first. Each literal piece of a reason is named here.
constexpr std::string_view fields_want_scalar = "a number";
constexpr std::string_view fields_want_numbers = "an array of numbers";
constexpr std::string_view fields_want_string = "a string";
constexpr std::string_view fields_part_component = "component '";
constexpr std::string_view fields_part_field = "' field '";
constexpr std::string_view fields_part_close = "'";
constexpr std::string_view fields_part_is = " is ";
constexpr std::string_view fields_part_item = " item ";
constexpr std::string_view fields_part_has = " has ";
constexpr std::string_view fields_part_sep = " ";
constexpr std::string_view fields_err_expected = ", expected ";
constexpr std::string_view fields_err_count = " numbers, expected ";
constexpr std::string_view fields_err_no_text = " has no source text for its numbers";
constexpr std::string_view fields_err_integer = " is not an integer";
constexpr std::string_view fields_err_channel = " is outside 0..255";
// The tint: three colour channels, each a whole number 0..255.
constexpr std::size_t fields_channel_count = 3;
constexpr int64_t fields_channel_max = 255;

// Internal: names the field in a reason.
inline std::string rv_editor_fields_where(const rv_editor_scene_component &c, std::string_view key)
{
    return std::string(fields_part_component) + c.name + std::string(fields_part_field) + std::string(key) +
        std::string(fields_part_close);
}

// Internal: names item i of count in a reason; a single number has no item.
inline std::string rv_editor_fields_item(std::size_t count, std::size_t i)
{
    return count == 1 ? std::string() : std::string(fields_part_item) + std::to_string(i);
}

// Internal: "<field> is <kind>, expected <want>".
inline std::string rv_editor_fields_kind_error(const rv_editor_scene_component &c,
    std::string_view key,
    rv_pdklib::rv_manifest_value_kind kind,
    std::string_view want)
{
    return rv_editor_fields_where(c, key) + std::string(fields_part_is) + std::string(rv_pdklib::rv_manifest_kind_name(kind)) +
        std::string(fields_err_expected) + std::string(want);
}

// Entry pointer of a component: const when the component is.
template <typename Component>
using rv_editor_fields_entry_ptr = std::
    conditional_t<std::is_const_v<Component>, const rv_pdklib::rv_manifest_tree_entry *, rv_pdklib::rv_manifest_tree_entry *>;

// The field with key, or nullptr; the first one when the key repeats. One body serves const and mutable.
template <typename Component>
inline auto rv_editor_fields_find(Component &c, std::string_view key) -> rv_editor_fields_entry_ptr<Component>
{
    for (auto &e : c.fields) {
        if (e.key == key) {
            return &e;
        }
    }
    return nullptr;
}

// The object's component named name, or nullptr. The pointer is valid until components grows.
inline rv_editor_scene_component *rv_editor_fields_find_component(rv_editor_scene_object &o, std::string_view name)
{
    for (rv_editor_scene_component &c : o.components) {
        if (c.name == name) {
            return &c;
        }
    }
    return nullptr;
}

// The object's component named name; when absent, an empty one is appended at the end (file order).
inline rv_editor_scene_component &rv_editor_fields_component_ensure(rv_editor_scene_object &o, std::string_view name)
{
    if (rv_editor_scene_component *found = rv_editor_fields_find_component(o, name)) {
        return *found;
    }
    rv_editor_scene_component added;
    added.name = std::string(name);
    o.components.push_back(std::move(added));
    return o.components.back();
}

// The writer keeps a number's text only when from_chars of it gives the same double (rv_scene_text_reads_back),
// so the double mirror is from_chars of the text. Texts here come from fx_to_text or std::to_string, so they read.
inline void rv_editor_fields_text_value(const std::string &text, double &value)
{
    std::from_chars(text.data(), text.data() + text.size(), value);
}

// Internal: an array of numbers from its texts, each value read from its text.
inline rv_pdklib::rv_manifest_mvalue rv_editor_fields_numbers_of(std::vector<std::string> texts)
{
    rv_pdklib::rv_manifest_mvalue v;
    v.kind = rv_pdklib::rv_manifest_value_kind::numbers;
    v.nums.resize(texts.size());
    for (std::size_t i = 0; i < texts.size(); ++i) {
        rv_editor_fields_text_value(texts[i], v.nums[i]);
    }
    v.nums_text = std::move(texts);
    return v;
}

// Internal: replaces the value of the field key, or appends the field at the end.
inline void rv_editor_fields_put(rv_editor_scene_component &c, std::string_view key, rv_pdklib::rv_manifest_mvalue value)
{
    rv_pdklib::rv_manifest_tree_entry *e = rv_editor_fields_find(c, key);
    if (e == nullptr) {
        rv_pdklib::rv_manifest_tree_entry added;
        added.key = std::string(key);
        c.fields.push_back(std::move(added));
        e = &c.fields.back();
    }
    e->value = std::move(value);
}

// Internal: true when the text has no fraction digit but zeros: "255" and "255.000" are whole, "255.0001" is not.
inline bool rv_editor_fields_is_whole(const std::string &text)
{
    const std::size_t dot = text.find('.');
    return dot == std::string::npos || text.find_first_not_of('0', dot + 1) == std::string::npos;
}

// Internal: the value holds N numbers: a scalar for N = 1, else an array with N source texts.
// RV_ERR_INVAL with the reason in error when it does not; a numbers value without nums_text has no source text.
template <std::size_t N>
inline int rv_editor_fields_check_shape(const rv_editor_scene_component &c,
    std::string_view key,
    const rv_pdklib::rv_manifest_mvalue &v,
    std::string_view want,
    std::string &error)
{
    if constexpr (N == 1) {
        const bool scalar =
            v.kind == rv_pdklib::rv_manifest_value_kind::integer || v.kind == rv_pdklib::rv_manifest_value_kind::real;
        if (!scalar) {
            error = rv_editor_fields_kind_error(c, key, v.kind, want);
            return RV_ERR_INVAL;
        }
        return RV_OK;
    }
    if (v.kind != rv_pdklib::rv_manifest_value_kind::numbers) {
        error = rv_editor_fields_kind_error(c, key, v.kind, want);
        return RV_ERR_INVAL;
    }
    if (v.nums_text.empty()) {
        error = rv_editor_fields_where(c, key) + std::string(fields_err_no_text);
        return RV_ERR_INVAL;
    }
    if (v.nums_text.size() != N) {
        error = rv_editor_fields_where(c, key) + std::string(fields_part_has) + std::to_string(v.nums_text.size()) +
            std::string(fields_err_count) + std::to_string(N);
        return RV_ERR_INVAL;
    }
    return RV_OK;
}

// Reads N Q20.12 numbers into out, written only on RV_OK. N = 1: an integer or real field. N = 3 or 4:
// an array of numbers with N texts. RV_ERR_NOENT when the field is absent; RV_ERR_INVAL when the kind or
// count is wrong or a text does not read (reason in error).
template <std::size_t N>
inline int rv_editor_fields_read_fx(const rv_editor_scene_component &c,
    std::string_view key,
    std::array<rv_pdklib::fx, N> &out,
    std::string &error)
{
    static_assert(N == 1 || N == 3 || N == 4, "a Q20.12 field is one number or an array of 3 or 4");
    const rv_pdklib::rv_manifest_tree_entry *e = rv_editor_fields_find(c, key);
    if (e == nullptr) {
        return RV_ERR_NOENT;
    }
    constexpr std::string_view want = N == 1 ? fields_want_scalar : fields_want_numbers;
    const int shape = rv_editor_fields_check_shape<N>(c, key, e->value, want, error);
    if (shape != RV_OK) {
        return shape;
    }
    const bool is_array = e->value.kind == rv_pdklib::rv_manifest_value_kind::numbers;
    std::array<rv_pdklib::fx, N> read{};
    for (std::size_t i = 0; i < N; ++i) {
        const std::string &text = is_array ? e->value.nums_text[i] : e->value.num_text;
        std::string reason;
        if (rv_pdklib::fx_from_text(text, read[i], reason) != RV_OK) {
            error = rv_editor_fields_where(c, key) + rv_editor_fields_item(N, i) + std::string(fields_part_sep) + reason;
            return RV_ERR_INVAL;
        }
    }
    out = read;
    return RV_OK;
}

// Writes N Q20.12 numbers to the field key, replacing it or appending it. N = 1 is a real field; N = 3 or 4
// an array of numbers. Each text is fx_to_text, the shortest that reads back.
template <std::size_t N>
inline void
rv_editor_fields_write_fx(rv_editor_scene_component &c, std::string_view key, const std::array<rv_pdklib::fx, N> &in)
{
    static_assert(N == 1 || N == 3 || N == 4, "a Q20.12 field is one number or an array of 3 or 4");
    if constexpr (N == 1) {
        rv_pdklib::rv_manifest_mvalue v;
        v.kind = rv_pdklib::rv_manifest_value_kind::real;
        v.num_text = rv_pdklib::fx_to_text(in[0]);
        rv_editor_fields_text_value(v.num_text, v.real);
        rv_editor_fields_put(c, key, std::move(v));
    } else {
        std::vector<std::string> texts;
        for (const rv_pdklib::fx raw : in) {
            texts.push_back(rv_pdklib::fx_to_text(raw));
        }
        rv_editor_fields_put(c, key, rv_editor_fields_numbers_of(std::move(texts)));
    }
}

// Reads a string field. RV_OK, RV_ERR_NOENT when absent, or RV_ERR_INVAL when the kind is not a string (reason in error).
inline int
rv_editor_fields_read_string(const rv_editor_scene_component &c, std::string_view key, std::string &out, std::string &error)
{
    const rv_pdklib::rv_manifest_tree_entry *e = rv_editor_fields_find(c, key);
    if (e == nullptr) {
        return RV_ERR_NOENT;
    }
    if (e->value.kind != rv_pdklib::rv_manifest_value_kind::string) {
        error = rv_editor_fields_kind_error(c, key, e->value.kind, fields_want_string);
        return RV_ERR_INVAL;
    }
    out = e->value.str;
    return RV_OK;
}

inline void rv_editor_fields_write_string(rv_editor_scene_component &c, std::string_view key, const std::string &value)
{
    rv_pdklib::rv_manifest_mvalue v;
    v.kind = rv_pdklib::rv_manifest_value_kind::string;
    v.str = value;
    rv_editor_fields_put(c, key, std::move(v));
}

// Reads the tint: three whole numbers 0..255. A text with a non-zero fraction digit is refused, "255.0"
// is accepted: as in rv_scene_tint of pdkkit's scene reader (pdk/kit/include/pdkkit/scene/rv_scene_read.hpp).
// Absence, kind and count errors are read_fx's. out is written only on RV_OK.
inline int rv_editor_fields_read_tint(const rv_editor_scene_component &c,
    std::string_view key,
    std::array<int, fields_channel_count> &out,
    std::string &error)
{
    const rv_pdklib::rv_manifest_tree_entry *e = rv_editor_fields_find(c, key);
    std::array<rv_pdklib::fx, fields_channel_count> raw{};
    const int read = rv_editor_fields_read_fx<fields_channel_count>(c, key, raw, error);
    if (read != RV_OK) {
        return read;
    }
    std::array<int, fields_channel_count> channels{};
    for (std::size_t i = 0; i < channels.size(); ++i) {
        if (!rv_editor_fields_is_whole(e->value.nums_text[i])) {
            error =
                rv_editor_fields_where(c, key) + rv_editor_fields_item(channels.size(), i) + std::string(fields_err_integer);
            return RV_ERR_INVAL;
        }
        if (raw[i] < 0 || raw[i] > fields_channel_max * rv_pdklib::fx_one) {
            error =
                rv_editor_fields_where(c, key) + rv_editor_fields_item(channels.size(), i) + std::string(fields_err_channel);
            return RV_ERR_INVAL;
        }
        channels[i] = raw[i] / rv_pdklib::fx_one;
    }
    out = channels;
    return RV_OK;
}

// Writes the tint as three integer texts (std::to_string). The caller keeps each channel 0..255.
inline void
rv_editor_fields_write_tint(rv_editor_scene_component &c, std::string_view key, const std::array<int, fields_channel_count> &in)
{
    std::vector<std::string> texts;
    for (const int channel : in) {
        texts.push_back(std::to_string(channel));
    }
    rv_editor_fields_put(c, key, rv_editor_fields_numbers_of(std::move(texts)));
}

} // namespace rv_editor
