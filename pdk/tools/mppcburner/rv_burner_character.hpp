#pragma once

namespace rv_pdktools
{

// Character classes of the manifest dialect, used by the lexer alone. '-' is an
// identifier character so a key may be spelled `include-dirs`.
bool rv_burner_is_ident(char c);
bool rv_burner_is_digit(char c);

} // namespace rv_pdktools
