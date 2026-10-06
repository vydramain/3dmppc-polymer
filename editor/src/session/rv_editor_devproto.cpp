// The dev protocol's framing and message shape, for the client side.

#include "session/rv_editor_devproto.hpp"

#include <charconv>

#include "pdk/rv_err.h"

namespace rv_editor
{

namespace
{

// Protocol response kind: successful reply from the console (src/rv_pconsole/rv_pconsole_cmd_devtools.cpp).
constexpr std::string_view devproto_ok_kind = "ok";

// Protocol response kind: error reply from the console (src/rv_pconsole/rv_pconsole_cmd_devtools.cpp).
constexpr std::string_view devproto_err_kind = "err";

// Event field name in protocol messages (src/rv_pconsole/rv_pconsole_cmd_devtools.cpp).
constexpr std::string_view devproto_event_field_name = "event";

// Field key-value separator in protocol messages.
constexpr char devproto_field_separator = '=';

// Protocol line terminator.
constexpr char devproto_line_terminator = '\n';

// Token separator in protocol line parsing.
constexpr char devproto_token_separator = ' ';

// Minimum word count for a valid protocol message (id + kind).
constexpr size_t devproto_min_words = 2;

// First field index in non-event messages; events start at index 1 (src/rv_pconsole/rv_pconsole_cmd_devtools.cpp).
constexpr size_t devproto_reply_fields_start = 2;

// First field index for event messages (id always 0, fields start at 1).
constexpr size_t devproto_event_fields_start = 1;

// Maximum bytes to include in error message context when truncating.
constexpr size_t devproto_error_context_max = 200;

// Separator between error description and protocol line context.
constexpr std::string_view devproto_error_separator = ": ";

// Hex digit range: '0' to '9'.
constexpr char hex_digit_zero = '0';
constexpr char hex_digit_nine = '9';

// Hex digit range: 'a' to 'f'.
constexpr char hex_digit_a = 'a';
constexpr char hex_digit_f = 'f';

// Hex digit offset: 'a' maps to 10 in base-16.
constexpr int hex_a_offset = 10;

// Hex digit byte width: 2 hex digits per byte.
constexpr size_t hex_digits_per_byte = 2;

// Hex byte conversion multiplier: 16^1 for high nibble.
constexpr int hex_nibble_multiplier = 16;

} // namespace

std::string_view rv_editor_devmsg::get(std::string_view key) const
{
    for (const auto &[k, v] : fields) {
        if (k == key) {
            return v;
        }
    }
    return {};
}

bool rv_editor_devmsg::has(std::string_view key) const
{
    for (const auto &field : fields) {
        if (field.first == key) {
            return true;
        }
    }
    return false;
}

int rv_editor_devmsg_parse(std::string_view line, rv_editor_devmsg &msg, std::string &error)
{
    std::vector<std::string_view> words;
    size_t p = 0;
    while (p < line.size()) {
        while (p < line.size() && line[p] == devproto_token_separator) {
            ++p;
        }
        const size_t q = line.find(devproto_token_separator, p);
        const size_t end = q == std::string_view::npos ? line.size() : q;
        if (end > p) {
            words.push_back(line.substr(p, end - p));
        }
        p = end;
    }
    if (words.size() < devproto_min_words) {
        error = "not a protocol line";
        return RV_ERR_INVAL;
    }

    int64_t id = -1;
    const auto [ptr, ec] = std::from_chars(words[0].data(), words[0].data() + words[0].size(), id);
    if (ec != std::errc{} || ptr != words[0].data() + words[0].size() || id < 0) {
        error = "no request id";
        return RV_ERR_INVAL;
    }

    rv_editor_devmsg m;
    m.id = id;
    size_t first_field = devproto_reply_fields_start;
    if (id == 0) {
        // An event names itself in its first field: `0 event=pause mode=paused`.
        m.kind = rv_editor_devmsg::rv_editor_devmsg_kind::event;
        first_field = devproto_event_fields_start;
    } else if (words[1] == devproto_ok_kind) {
        m.kind = rv_editor_devmsg::rv_editor_devmsg_kind::ok;
    } else if (words[1] == devproto_err_kind) {
        m.kind = rv_editor_devmsg::rv_editor_devmsg_kind::err;
    } else {
        error = "reply is neither ok nor err";
        return RV_ERR_INVAL;
    }

    for (size_t i = first_field; i < words.size(); ++i) {
        const size_t eq = words[i].find(devproto_field_separator);
        if (eq == std::string_view::npos || eq == 0) {
            error = "field without key=value";
            return RV_ERR_INVAL;
        }
        m.fields.emplace_back(std::string(words[i].substr(0, eq)),
            std::string(words[i].substr(eq + 1)));
    }
    if (m.kind == rv_editor_devmsg::rv_editor_devmsg_kind::event &&
        !m.has(devproto_event_field_name)) {
        error = "event without a name";
        return RV_ERR_INVAL;
    }
    msg = std::move(m);
    return RV_OK;
}

void rv_editor_devparser::feed(std::string_view bytes, std::vector<rv_editor_devmsg> &out,
    std::vector<std::string> &errors)
{
    while (!bytes.empty()) {
        const size_t nl = bytes.find(devproto_line_terminator);
        const std::string_view piece = bytes.substr(0, nl);
        bytes = nl == std::string_view::npos ? std::string_view{} : bytes.substr(nl + 1);

        if (!skipping_) {
            if (line_.size() + piece.size() > line_max) {
                errors.push_back("a line over " + std::to_string(line_max) + " bytes was dropped");
                line_.clear();
                skipping_ = true;
            } else {
                line_.append(piece);
            }
        }
        if (nl == std::string_view::npos) {
            return;
        }
        if (skipping_) {
            skipping_ = false;
            continue;
        }

        rv_editor_devmsg msg;
        std::string error;
        if (rv_editor_devmsg_parse(line_, msg, error) == RV_OK) {
            out.push_back(std::move(msg));
        } else {
            errors.push_back(error + std::string(devproto_error_separator) +
                line_.substr(0, devproto_error_context_max));
        }
        line_.clear();
    }
}

std::string rv_editor_hex_decode(std::string_view hex)
{
    auto nibble = [](char c) -> int {
        if (c >= hex_digit_zero && c <= hex_digit_nine) {
            return c - hex_digit_zero;
        }
        if (c >= hex_digit_a && c <= hex_digit_f) {
            return c - hex_digit_a + hex_a_offset;
        }
        return -1;
    };
    std::string out;
    for (size_t i = 0; i + 1 < hex.size(); i += hex_digits_per_byte) {
        const int hi = nibble(hex[i]);
        const int lo = nibble(hex[i + 1]);
        if (hi < 0 || lo < 0) {
            break;
        }
        out.push_back(static_cast<char>(hi * hex_nibble_multiplier + lo));
    }
    return out;
}

} // namespace rv_editor
