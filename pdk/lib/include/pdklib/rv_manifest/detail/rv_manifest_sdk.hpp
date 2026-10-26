#pragma once

#include <string>

#include "rv_manifest_character.hpp"

namespace rv_pdklib
{

// [build] sdk names a directory next to the pdk contract, so the name is a plain
// word: a lowercase letter, then lowercase letters, digits or '_'. Nothing that
// could climb out of the contract ('/', '.') or hide a directory gets through.
// rv_manifest_validate applies this check; rv_manifest_parse does not call it.
constexpr char RV_MANIFEST_SDK_LETTER_MIN = 'a';
constexpr char RV_MANIFEST_SDK_LETTER_MAX = 'z';
constexpr char RV_MANIFEST_SDK_UNDERSCORE = '_';

inline bool rv_manifest_sdk_letter(char c)
{
    return c >= RV_MANIFEST_SDK_LETTER_MIN && c <= RV_MANIFEST_SDK_LETTER_MAX;
}

inline bool rv_manifest_sdk_name_is_valid(const std::string &name)
{
    if (name.empty() || !rv_manifest_sdk_letter(name.front())) {
        return false;
    }
    for (char c : name) {
        if (!rv_manifest_sdk_letter(c) && !rv_manifest_is_digit(c) && c != RV_MANIFEST_SDK_UNDERSCORE) {
            return false;
        }
    }
    return true;
}

} // namespace rv_pdklib
