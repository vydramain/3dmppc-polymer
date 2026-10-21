#pragma once

#include <cctype>

namespace rv_pdklib
{

// Character classes of the manifest dialect, used by the lexer alone. '-' is an
// identifier character so a key may be spelled `include-dirs`.
inline bool rv_manifest_is_ident(char c)
{
    const unsigned char u = static_cast<unsigned char>(c);
    return std::isalnum(u) != 0 || c == '_' || c == '-' || c == '.';
}

inline bool rv_manifest_is_digit(char c)
{
    return std::isdigit(static_cast<unsigned char>(c)) != 0;
}

} // namespace rv_pdklib
