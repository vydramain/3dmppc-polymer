// Lowercase hex encoding and the error line of the development protocol. Nothing outside the
// development runtime speaks it, so rv_pcdevhex.cpp is compiled only into a
// -D3DMPPC_DEVTOOLS=ON build (see CMakeLists.txt). It needs no null half, unlike
// the slots around it: a player build calls none of them, so leaving the
// declarations visible costs that build nothing and defining them would.
#pragma once

#include <cstdint>
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

// One `err` answer: `<id> err error=<token> rv_err=<rc> effects=<0|1> msg=<hex>`,
// the message cut and encoded by rv_pcdev_hex_msg.
std::string rv_pcdev_err(int64_t id, const char *token, int64_t rc, bool effects, std::string_view message);

} // namespace rv_3dmppc
