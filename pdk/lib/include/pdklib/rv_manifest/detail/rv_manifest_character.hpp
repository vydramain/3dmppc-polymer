#pragma once

namespace rv_pdklib
{

// Character classes of the manifest dialect, used by the lexer alone. '-' is an
// identifier character so a key may be spelled `include-dirs`.
bool rv_manifest_is_ident(char c);
bool rv_manifest_is_digit(char c);

} // namespace rv_pdklib
