#include "rv_pconsole/platform/rv_pcdevhex.hpp"

#include <cstddef>
#include <cstdint>
#include <format>
#include <string>

#include "rv_pconsole/platform/rv_pcdevchan.hpp"

namespace rv_3dmppc
{

std::string rv_pcdev_hex(std::string_view bytes)
{
    static constexpr char digits[] = "0123456789abcdef";
    std::string out;
    out.reserve(bytes.size() * 2);
    for (const char c : bytes) {
        const unsigned char byte = static_cast<unsigned char>(c);
        out.push_back(digits[byte >> 4]);
        out.push_back(digits[byte & 0x0F]);
    }
    return out;
}

std::string rv_pcdev_hex_msg(std::string_view message)
{
    static constexpr std::string_view cut = " ...(truncated)";
    if (message.size() <= static_cast<std::size_t>(RV_PCDEVCHAN_MSG_MAX)) {
        return rv_pcdev_hex(message);
    }
    // The head, not the tail: a lua error puts the chunk name and the line
    // number first, and those are the part worth keeping.
    std::string cut_down(message.substr(0, static_cast<std::size_t>(RV_PCDEVCHAN_MSG_MAX)));
    cut_down.append(cut);
    return rv_pcdev_hex(cut_down);
}

std::string rv_pcdev_err(int64_t id, const char *token, int64_t rc, bool effects,
    std::string_view message)
{
    return std::format("{} err error={} rv_err={} effects={} msg={}", id, token, rc, effects ? 1 : 0,
        rv_pcdev_hex_msg(message));
}

} // namespace rv_3dmppc
