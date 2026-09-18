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

// Hex of a DIAGNOSTIC string, cut to RV_PCDEVCHAN_MSG_MAX bytes first with the
// cut named in the text. Used for every string the DISC wrote - a lua error, an
// attach() refusal reason - because their length is the disc's choice and an
// answer that does not fit the queue is an answer the client never sees. A
// stored value read by `get` does NOT come through here: that one is data the
// client asked for, so an oversized one is refused rather than shortened.
std::string rv_pcdev_hex_msg(std::string_view message);

} // namespace rv_3dmppc
