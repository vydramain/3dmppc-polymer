// Lowercase hex encoding used by the development protocol. Plain formatting,
// not developer policy, so it is always compiled - not excluded in a player
// build the way the channel itself is.
#pragma once

#include <string>
#include <string_view>

namespace rv_3dmppc
{

// Lowercase hex of `bytes`. Every value that could carry a space, a newline or a
// NUL travels through this: a Lua error message is multi-line by nature, and a
// protocol that needs escaping rules needs a parser, while hex needs neither.
std::string rv_pcdev_hex(std::string_view bytes);

} // namespace rv_3dmppc
