// The dev protocol's framing and message shape, for the client side.

#include "session/rv_editor_devproto.hpp"

#include <charconv>

namespace rv_editor
{

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

bool rv_editor_devmsg_parse(std::string_view line, rv_editor_devmsg &msg, std::string &error)
{
    std::vector<std::string_view> words;
    size_t p = 0;
    while (p < line.size()) {
        while (p < line.size() && line[p] == ' ') {
            ++p;
        }
        const size_t q = line.find(' ', p);
        const size_t end = q == std::string_view::npos ? line.size() : q;
        if (end > p) {
            words.push_back(line.substr(p, end - p));
        }
        p = end;
    }
    if (words.size() < 2) {
        error = "not a protocol line";
        return false;
    }

    int64_t id = -1;
    const auto [ptr, ec] = std::from_chars(words[0].data(), words[0].data() + words[0].size(), id);
    if (ec != std::errc{} || ptr != words[0].data() + words[0].size() || id < 0) {
        error = "no request id";
        return false;
    }

    rv_editor_devmsg m;
    m.id = id;
    size_t first_field = 2;
    if (id == 0) {
        // An event names itself in its first field: `0 event=pause mode=paused`.
        m.kind = rv_editor_devmsg::rv_editor_devmsg_kind::event;
        first_field = 1;
    } else if (words[1] == "ok") {
        m.kind = rv_editor_devmsg::rv_editor_devmsg_kind::ok;
    } else if (words[1] == "err") {
        m.kind = rv_editor_devmsg::rv_editor_devmsg_kind::err;
    } else {
        error = "reply is neither ok nor err";
        return false;
    }

    for (size_t i = first_field; i < words.size(); ++i) {
        const size_t eq = words[i].find('=');
        if (eq == std::string_view::npos || eq == 0) {
            error = "field without key=value";
            return false;
        }
        m.fields.emplace_back(std::string(words[i].substr(0, eq)), std::string(words[i].substr(eq + 1)));
    }
    if (m.kind == rv_editor_devmsg::rv_editor_devmsg_kind::event && !m.has("event")) {
        error = "event without a name";
        return false;
    }
    msg = std::move(m);
    return true;
}

void rv_editor_devparser::feed(std::string_view bytes, std::vector<rv_editor_devmsg> &out,
    std::vector<std::string> &errors)
{
    while (!bytes.empty()) {
        const size_t nl = bytes.find('\n');
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
        if (rv_editor_devmsg_parse(line_, msg, error)) {
            out.push_back(std::move(msg));
        } else {
            errors.push_back(error + ": " + line_.substr(0, 200));
        }
        line_.clear();
    }
}

std::string rv_editor_hex_decode(std::string_view hex)
{
    auto nibble = [](char c) -> int {
        if (c >= '0' && c <= '9') {
            return c - '0';
        }
        if (c >= 'a' && c <= 'f') {
            return c - 'a' + 10;
        }
        return -1;
    };
    std::string out;
    for (size_t i = 0; i + 1 < hex.size(); i += 2) {
        const int hi = nibble(hex[i]);
        const int lo = nibble(hex[i + 1]);
        if (hi < 0 || lo < 0) {
            break;
        }
        out.push_back(static_cast<char>(hi * 16 + lo));
    }
    return out;
}

} // namespace rv_editor
