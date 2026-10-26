#pragma once

// Decimal text of Q20.12 for scene files, read and written as integers: no double takes part.
// Text grammar: -?digits(.digits)? with up to 12 fraction digits; no exponent, no '+'.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>

#include <pdk/rv_err.h>

#include "rv_fx.hpp"

namespace rv_pdklib
{

// Longest fraction a text may carry. 2^12 divides 10^12, so 12 digits name every raw step exactly.
constexpr int fx_text_frac_digits_max = 12;
// Whole part saturates here (2^20 units, beyond the 2^19 of fx): the digit loop never overflows,
// and a saturated value still fails the range check.
constexpr uint64_t fx_text_whole_cap = uint64_t{ 1 } << 20;
// Failure reasons for fx_from_text; the caller prints them after the field name.
constexpr const char *fx_text_err_syntax = "is not a plain decimal: -?digits(.digits)?, no exponent, short fraction";
constexpr const char *fx_text_err_range = "is outside the Q20.12 range";

// Internal: true for '0'..'9'.
inline bool fx_text_is_digit(char c)
{
    return c >= '0' && c <= '9';
}

// Internal: whole digits of s[from, to): at least one, all digits.
inline bool fx_text_whole(const std::string &s, std::size_t from, std::size_t to, uint64_t &whole)
{
    whole = 0;
    if (from == to) {
        return false;
    }
    for (std::size_t i = from; i < to; ++i) {
        if (!fx_text_is_digit(s[i])) {
            return false;
        }
        whole = std::min(whole * 10 + static_cast<uint64_t>(s[i] - '0'), fx_text_whole_cap);
    }
    return true;
}

// Internal: fraction digits from `from` to the end of s: 1..12 digits. raw is the fraction in
// Q20.12 steps, rounded half up; the fraction is not negative, so this is half away from zero.
inline bool fx_text_fraction(const std::string &s, std::size_t from, uint64_t &raw)
{
    const std::size_t len = s.size() - from;
    if (len == 0 || len > static_cast<std::size_t>(fx_text_frac_digits_max)) {
        return false;
    }
    uint64_t num = 0;
    uint64_t den = 1;
    for (std::size_t i = from; i < s.size(); ++i) {
        if (!fx_text_is_digit(s[i])) {
            return false;
        }
        num = num * 10 + static_cast<uint64_t>(s[i] - '0');
        den *= 10;
    }
    const uint64_t unit = static_cast<uint64_t>(fx_one);
    raw = (2 * num * unit + den) / (2 * den);
    return true;
}

// Reads -?digits(.digits)? into raw Q20.12, rounding half away from zero. Error: the text is
// outside the grammar, or its value leaves fx; error holds the reason, out is untouched.
inline int fx_from_text(const std::string &text, fx &out, std::string &error)
{
    const bool negative = !text.empty() && text[0] == '-';
    const std::size_t begin = negative ? 1 : 0;
    const std::size_t point = text.find('.', begin);
    const std::size_t whole_end = point == std::string::npos ? text.size() : point;
    uint64_t whole = 0;
    uint64_t frac = 0;
    const bool frac_ok = point == std::string::npos || fx_text_fraction(text, point + 1, frac);
    if (!fx_text_whole(text, begin, whole_end, whole) || !frac_ok) {
        error = fx_text_err_syntax;
        return RV_ERR_INVAL;
    }
    const int64_t mag = static_cast<int64_t>(whole) * fx_one + static_cast<int64_t>(frac);
    const int64_t raw = negative ? -mag : mag;
    if (!fx_fits(raw)) {
        error = fx_text_err_range;
        return RV_ERR_INVAL;
    }
    out = static_cast<fx>(raw);
    return RV_OK;
}

// Internal: text of the magnitude with k fraction digits; the fraction rounds half up to 10^-k,
// and a carry goes to the whole part.
inline std::string fx_text_with_digits(bool negative, uint64_t whole, uint64_t frac, int k)
{
    const uint64_t unit = static_cast<uint64_t>(fx_one);
    uint64_t scale = 1;
    for (int i = 0; i < k; ++i) {
        scale *= 10;
    }
    uint64_t digits = (2 * frac * scale + unit) / (2 * unit);
    whole += digits / scale;
    digits %= scale;
    std::string text = negative ? "-" : "";
    text += std::to_string(whole);
    if (k == 0) {
        return text;
    }
    std::string fraction(static_cast<std::size_t>(k), '0');
    for (int i = k - 1; i >= 0; --i) {
        fraction[static_cast<std::size_t>(i)] = static_cast<char>('0' + digits % 10);
        digits /= 10;
    }
    return text + "." + fraction;
}

// Shortest text (k = 0..12 fraction digits) that fx_from_text reads back to raw. At k = 12 the
// text is exact, since 10^12 / 4096 = 5^12, so the loop always stops there.
inline std::string fx_to_text(fx raw)
{
    const int64_t wide = raw;
    const uint64_t mag = static_cast<uint64_t>(wide < 0 ? -wide : wide);
    const uint64_t whole = mag / static_cast<uint64_t>(fx_one);
    const uint64_t frac = mag % static_cast<uint64_t>(fx_one);
    std::string text;
    std::string error;
    for (int k = 0; k <= fx_text_frac_digits_max; ++k) {
        text = fx_text_with_digits(raw < 0, whole, frac, k);
        fx back = 0;
        if (fx_from_text(text, back, error) == RV_OK && back == raw) {
            break;
        }
    }
    return text;
}

} // namespace rv_pdklib
