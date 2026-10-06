#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace rv_editor
{

// The development console's line protocol as the client reads and writes it (README.md,
// "The channel"): `<id> ok k=v ...`, `<id> err error=<token> ...`, and events
// the console raises on its own, `0 event=<name> k=v ...`. No I/O here: bytes go
// in as they arrive, whole messages come out.

struct rv_editor_devmsg
{
    enum class rv_editor_devmsg_kind
    {
        ok,
        err,
        event,
    };

    rv_editor_devmsg_kind kind = rv_editor_devmsg_kind::ok;
    int64_t id = 0;
    std::vector<std::pair<std::string, std::string>> fields; // in the order sent

    // Value of `key`, or an empty view when it is absent.
    std::string_view get(std::string_view key) const;
    bool has(std::string_view key) const;
};

class rv_editor_devparser
{
public:
    // A line longer than this is not a reply this client can hold: it is
    // dropped whole and reported, and reading goes on at the next line.
    static constexpr size_t line_max = 64 * 1024;

    // Consumes `bytes`, which may end mid-line or hold several lines. Complete
    // messages go to `out`; each line that is not one goes to `errors`.
    void feed(std::string_view bytes, std::vector<rv_editor_devmsg> &out, std::vector<std::string> &errors);

private:
    std::string line_;
    bool skipping_ = false;
};

// Parses one line without its newline. Returns RV_OK on success, RV_ERR_INVAL with reason in `error`.
int rv_editor_devmsg_parse(std::string_view line, rv_editor_devmsg &msg, std::string &error);

// The protocol's lowercase hex back to bytes; stops at the first non-hex pair.
std::string rv_editor_hex_decode(std::string_view hex);

// Commands the client sends (src/rv_pconsole/rv_pconsole_cmd_devtools.cpp).
inline constexpr std::string_view cmd_status = "status";
inline constexpr std::string_view cmd_pause = "pause";
inline constexpr std::string_view cmd_resume = "resume";
inline constexpr std::string_view cmd_step = "step";
inline constexpr std::string_view cmd_quit = "quit";

} // namespace rv_editor
