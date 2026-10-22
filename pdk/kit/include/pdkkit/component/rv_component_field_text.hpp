#pragma once

// Text of a component field value (ecs_scene_field.value) parsed into the field's bytes. A parser writes out only on
// success, so a refusal leaves the destination untouched.

#include <charconv>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <system_error>

#include "pdk/cv/rv_vertex.h"
#include "pdk/rv_err.h"
#include "pdkkit/component/rv_component_desc.hpp"
#include "pdkkit/ecs/rv_ecs_scene_records.hpp"
#include "pdklib/rv_math/rv_fx.hpp"
#include "pdklib/rv_math/rv_fx_text.hpp"

namespace rv_pdkkit
{

// Bytes that wrap a value's text: the brackets of "[a, b, c]", or the two quotes of a string or model.
inline constexpr std::size_t ecs_wrap_size = 2;
// Largest colour channel: channels are bytes.
inline constexpr int32_t ecs_channel_max = 255;
// Wraps a string or model value; a quote inside must be escaped.
inline constexpr char ecs_quote = '"';
inline constexpr char ecs_escape = '\\';

// Escapes of a quoted value: the five rv_scene_quote writes, the inverse of the manifest lexer.
struct ecs_text_escape {
    char code;
    char byte;
};

inline constexpr ecs_text_escape ecs_text_escapes[] = {
    { '"', '"' },
    { '\\', '\\' },
    { 'n', '\n' },
    { 't', '\t' },
    { 'r', '\r' },
};

// Failure reasons; the caller prints them after the field name.
inline constexpr const char *ecs_err_int = "is not a whole number";
inline constexpr const char *ecs_err_range = "is outside its range";
inline constexpr const char *ecs_err_array = "is not [a, b, c] with three items";
inline constexpr const char *ecs_err_wrap = "is not a double-quoted text";
inline constexpr const char *ecs_err_quote = "has a quote that is not escaped";
inline constexpr const char *ecs_err_nul = "has a NUL byte";
inline constexpr const char *ecs_err_escape = "has an escape other than \\\" \\\\ \\n \\t \\r";
inline constexpr const char *ecs_err_backslash = "ends with a backslash";
inline constexpr const char *ecs_err_long = "decodes to more than ecs_value_capacity - 1 bytes";
inline constexpr const char *ecs_err_unknown_type = "is not a field type of the table";

// Internal: `text` without spaces at either end.
inline std::string_view ecs_field_trim(std::string_view text)
{
    const std::size_t first = text.find_first_not_of(' ');
    if (first == std::string_view::npos) {
        return {};
    }
    return text.substr(first, text.find_last_not_of(' ') - first + 1);
}

// Internal: the items of "[a, b, c]" into parts, trimmed. Error: not bracketed, or not exactly three items.
inline int ecs_field_split(std::string_view text, std::string_view (&parts)[ecs_array_items], std::string &error)
{
    if (text.size() < ecs_wrap_size || text.front() != '[' || text.back() != ']') {
        error = ecs_err_array;
        return RV_ERR_INVAL;
    }
    std::string_view rest = text.substr(1, text.size() - ecs_wrap_size);
    for (std::size_t i = 0; i + 1 < ecs_array_items; ++i) {
        const std::size_t comma = rest.find(',');
        if (comma == std::string_view::npos) {
            error = ecs_err_array;
            return RV_ERR_INVAL;
        }
        parts[i] = ecs_field_trim(rest.substr(0, comma));
        rest.remove_prefix(comma + 1);
    }
    if (rest.find(',') != std::string_view::npos) {
        error = ecs_err_array;
        return RV_ERR_INVAL;
    }
    parts[ecs_array_items - 1] = ecs_field_trim(rest);
    return RV_OK;
}

// Internal: an int32 from the whole of text. Error: not a number, or outside int32; value is untouched.
inline int ecs_field_int(std::string_view text, int32_t &value, std::string &error)
{
    int32_t read = 0;
    const char *last = text.data() + text.size();
    const std::from_chars_result result = std::from_chars(text.data(), last, read);
    if (result.ec == std::errc::result_out_of_range) {
        error = ecs_err_range;
        return RV_ERR_INVAL;
    }
    if (result.ec != std::errc{} || result.ptr != last) {
        error = ecs_err_int;
        return RV_ERR_INVAL;
    }
    value = read;
    return RV_OK;
}

// Internal: one colour channel, 0..255. Error: not a number, or outside 0..255; value is untouched.
inline int ecs_field_channel(std::string_view text, uint8_t &value, std::string &error)
{
    int32_t wide = 0;
    const int read = ecs_field_int(text, wide, error);
    if (read != RV_OK) {
        return read;
    }
    if (wide < 0 || wide > ecs_channel_max) {
        error = ecs_err_range;
        return RV_ERR_INVAL;
    }
    value = static_cast<uint8_t>(wide);
    return RV_OK;
}

// Parsers: text is one scene field value; out holds the field's bytes and is written only on success.
inline int ecs_field_parse_int32(std::string_view text, unsigned char *out, std::string &error)
{
    int32_t value = 0;
    const int read = ecs_field_int(text, value, error);
    if (read != RV_OK) {
        return read;
    }
    std::memcpy(out, &value, sizeof(value));
    return RV_OK;
}

// Q20.12 from decimal text; fx_from_text takes no double.
inline int ecs_field_parse_fixed(std::string_view text, unsigned char *out, std::string &error)
{
    rv_pdklib::fx value = 0;
    const int read = rv_pdklib::fx_from_text(std::string(text), value, error);
    if (read != RV_OK) {
        return read;
    }
    std::memcpy(out, &value, sizeof(value));
    return RV_OK;
}

// "[a, b, c]" of Q20.12 numbers, for fixed3 and angle3 (degrees is the meaning, not the layout).
inline int ecs_field_parse_fx3(std::string_view text, unsigned char *out, std::string &error)
{
    std::string_view parts[ecs_array_items] = {};
    const int split = ecs_field_split(text, parts, error);
    if (split != RV_OK) {
        return split;
    }
    rv_pdklib::fx values[ecs_array_items] = {};
    for (std::size_t i = 0; i < ecs_array_items; ++i) {
        const int read = rv_pdklib::fx_from_text(std::string(parts[i]), values[i], error);
        if (read != RV_OK) {
            return read;
        }
    }
    std::memcpy(out, values, sizeof(values));
    return RV_OK;
}

// "[r, g, b]" with each channel 0..255.
inline int ecs_field_parse_color(std::string_view text, unsigned char *out, std::string &error)
{
    std::string_view parts[ecs_array_items] = {};
    const int split = ecs_field_split(text, parts, error);
    if (split != RV_OK) {
        return split;
    }
    uint8_t channels[ecs_array_items] = {};
    for (std::size_t i = 0; i < ecs_array_items; ++i) {
        const int read = ecs_field_channel(parts[i], channels[i], error);
        if (read != RV_OK) {
            return read;
        }
    }
    const rv_color color{ channels[0], channels[1], channels[2] };
    std::memcpy(out, &color, sizeof(color));
    return RV_OK;
}

// Internal: the byte of escape `code`, from ecs_text_escapes; false when the table has no such escape.
inline bool ecs_text_unescape_code(char code, char &byte)
{
    for (const ecs_text_escape &escape : ecs_text_escapes) {
        if (escape.code == code) {
            byte = escape.byte;
            return true;
        }
    }
    return false;
}

// Internal: the decoded characters between the quotes. Error: a bare quote, a NUL, an unknown escape, or a trailing
// backslash; decoded is untouched.
inline int ecs_text_unescape(std::string_view inner, std::string &decoded, std::string &error)
{
    std::string out;
    for (std::size_t i = 0; i < inner.size(); ++i) {
        const char c = inner[i];
        if (c == ecs_quote) {
            error = ecs_err_quote;
            return RV_ERR_INVAL;
        }
        if (c == '\0') {
            error = ecs_err_nul;
            return RV_ERR_INVAL;
        }
        if (c != ecs_escape) {
            out += c;
            continue;
        }
        if (i + 1 == inner.size()) {
            error = ecs_err_backslash;
            return RV_ERR_INVAL;
        }
        char byte = 0;
        if (!ecs_text_unescape_code(inner[++i], byte)) {
            error = ecs_err_escape;
            return RV_ERR_INVAL;
        }
        out += byte;
    }
    decoded = out;
    return RV_OK;
}

// "text" in double quotes: the decoded text must fit ecs_value_capacity with its NUL.
inline int ecs_field_parse_quoted(std::string_view text, unsigned char *out, std::string &error)
{
    if (text.size() < ecs_wrap_size || text.front() != ecs_quote || text.back() != ecs_quote) {
        error = ecs_err_wrap;
        return RV_ERR_INVAL;
    }
    std::string decoded;
    const int unescaped = ecs_text_unescape(text.substr(1, text.size() - ecs_wrap_size), decoded, error);
    if (unescaped != RV_OK) {
        return unescaped;
    }
    if (decoded.size() >= ecs_value_capacity) {
        error = ecs_err_long;
        return RV_ERR_INVAL;
    }
    std::memset(out, 0, ecs_value_capacity);
    std::memcpy(out, decoded.data(), decoded.size());
    return RV_OK;
}

// The parser of each field type.
using ecs_field_parse_fn = int (*)(std::string_view text, unsigned char *out, std::string &error);

struct ecs_field_parser {
    ecs_field_type type;
    ecs_field_parse_fn parse;
};

inline constexpr ecs_field_parser ecs_field_parsers[] = {
    { ecs_field_type::int32, ecs_field_parse_int32 },
    { ecs_field_type::fixed, ecs_field_parse_fixed },
    { ecs_field_type::fixed3, ecs_field_parse_fx3 },
    { ecs_field_type::angle3, ecs_field_parse_fx3 },
    { ecs_field_type::color, ecs_field_parse_color },
    { ecs_field_type::string, ecs_field_parse_quoted },
    { ecs_field_type::model, ecs_field_parse_quoted },
};

// Parses `text` as a value of `type` into out, which holds the type's size. Written only on success.
inline int ecs_field_parse(ecs_field_type type, std::string_view text, unsigned char *out, std::string &error)
{
    for (const ecs_field_parser &row : ecs_field_parsers) {
        if (row.type == type) {
            return row.parse(text, out, error);
        }
    }
    error = ecs_err_unknown_type;
    return RV_ERR_INVAL;
}

} // namespace rv_pdkkit
