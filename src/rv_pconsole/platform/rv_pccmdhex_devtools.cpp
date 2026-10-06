#include "rv_pconsole/platform/rv_pccmdhex.hpp"

#include <cstddef>
#include <cstdint>
#include <format>
#include <string>

#include "rv_pconsole/platform/rv_pccmdchan.hpp"

namespace rv_3dmppc
{
namespace
{

// Hex encoding constants.
constexpr std::size_t HEX_CHARS_PER_BYTE = 2; // Two hex digits represent one byte
constexpr int HIGH_NIBBLE_SHIFT = 4;          // Bit shift to extract high nibble
constexpr uint8_t NIBBLE_MASK = 0x0F;         // Mask to extract low nibble

} // namespace

std::string rv_pccmd_hex(std::string_view bytes)
{
    static constexpr char digits[] = "0123456789abcdef";
    std::string out;
    out.reserve(bytes.size() * HEX_CHARS_PER_BYTE);
    for (const char c : bytes) {
        const unsigned char byte = static_cast<unsigned char>(c);
        out.push_back(digits[byte >> HIGH_NIBBLE_SHIFT]);
        out.push_back(digits[byte & NIBBLE_MASK]);
    }
    return out;
}

std::string rv_pccmd_hex_msg(std::string_view message)
{
    static constexpr std::string_view cut = " ...(truncated)";
    if (message.size() <= static_cast<std::size_t>(RV_PCCMDCHAN_MSG_MAX)) {
        return rv_pccmd_hex(message);
    }
    // The head, not the tail: a lua error puts the chunk name and the line
    // number first, and those are the part worth keeping.
    std::string cut_down(message.substr(0, static_cast<std::size_t>(RV_PCCMDCHAN_MSG_MAX)));
    cut_down.append(cut);
    return rv_pccmd_hex(cut_down);
}

std::string rv_pccmd_err(int64_t id, const char *token, int64_t rc, bool effects,
    std::string_view message)
{
    return std::format("{} err error={} rv_err={} effects={} msg={}", id, token, rc, effects ? 1 : 0,
        rv_pccmd_hex_msg(message));
}

} // namespace rv_3dmppc
