// Places in build output: what Problems lists and opens.

#include "project/rv_editor_problems.hpp"

#include <charconv>

#include "pdk/rv_err.h"

namespace rv_editor
{

namespace
{

// The number at the end of `s` after its last ':', and `s` without it; RV_OK on success, RV_ERR_INVAL
// when there is no valid number.
int rv_editor_take_number(std::string_view &s, int32_t &n)
{
    const size_t colon = s.rfind(':');
    if (colon == std::string_view::npos || colon + 1 >= s.size()) {
        return RV_ERR_INVAL;
    }
    const std::string_view digits = s.substr(colon + 1);
    const auto [end, ec] = std::from_chars(digits.data(), digits.data() + digits.size(), n);
    if (ec != std::errc{} || end != digits.data() + digits.size() || n <= 0) {
        return RV_ERR_INVAL;
    }
    s = s.substr(0, colon);
    return RV_OK;
}

std::filesystem::path rv_editor_resolve(std::string_view file, const std::filesystem::path &root)
{
    const std::filesystem::path p(file);
    return p.is_absolute() ? p : root / p;
}

} // namespace

bool rv_editor_problem_parse(std::string_view text, const std::filesystem::path &root, rv_editor_problem &out)
{
    // mppcburner: cannot compile 'scripts/x.lua': ...chunk/x.lua:233: '=' expected near 'lua'
    constexpr std::string_view burner = "cannot compile '";
    if (const size_t at = text.find(burner); at != std::string_view::npos) {
        const size_t name = at + burner.size();
        const size_t quote = text.find('\'', name);
        if (quote == std::string_view::npos) {
            return false;
        }
        // The chunk name may be cut short with "..."; the quoted path is the file.
        const std::string_view rest = text.substr(quote + 1);
        for (size_t colon = rest.find(':'); colon != std::string_view::npos; colon = rest.find(':', colon + 1)) {
            std::string_view head = rest.substr(0, colon);
            int32_t line = 0;
            const size_t digits_at = head.find_last_not_of("0123456789");
            if (digits_at != std::string_view::npos && head[digits_at] == ':' &&
                rv_editor_take_number(head, line) == RV_OK) {
                out = { rv_editor_resolve(text.substr(name, quote - name), root), line, 0, true,
                    std::string(rest.substr(colon + 1)) };
                while (!out.message.empty() && out.message.front() == ' ') {
                    out.message.erase(0, 1);
                }
                return true;
            }
        }
        return false;
    }

    // <file>:<line>[:<col>]: error: <text>   (gcc and clang)
    constexpr std::string_view kinds[] = { ": fatal error: ", ": error: ", ": warning: " };
    for (const std::string_view kind : kinds) {
        const size_t at = text.find(kind);
        if (at == std::string_view::npos) {
            continue;
        }
        std::string_view place = text.substr(0, at);
        int32_t first = 0;
        int32_t second = 0;
        if (rv_editor_take_number(place, first) != RV_OK) {
            return false;
        }
        const bool has_column = rv_editor_take_number(place, second) == RV_OK;
        if (place.empty()) {
            return false;
        }
        out = { rv_editor_resolve(place, root), has_column ? second : first, has_column ? first : 0,
            kind != ": warning: ", std::string(text.substr(at + kind.size())) };
        return true;
    }
    return false;
}

} // namespace rv_editor
