#include "rv_pconsole/platform/rv_pcdevhex.hpp"

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

} // namespace rv_3dmppc
