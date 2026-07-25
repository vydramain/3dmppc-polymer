#include "rv_log.hpp"

#include <cstdio>

namespace rv_3dmppc {

// Console-only debug log. Verbosity baked for now; DBG stays silent.
static rv_log_level g_threshold = RV_LOG_LEVEL_INFO;

static const char* level_name(rv_log_level lvl) {
    switch (lvl) {
        case RV_LOG_LEVEL_EMERG:
            return "EMERG";
        case RV_LOG_LEVEL_ERR:
            return "ERR";
        case RV_LOG_LEVEL_WARN:
            return "WARN";
        case RV_LOG_LEVEL_INFO:
            return "INFO";
        case RV_LOG_LEVEL_DBG:
            return "DBG";
    }
    return "?";
}

std::string rv_log_escape(const char* text, std::size_t max_length) {
    if (!text) return "(null)";

    std::string out;
    std::size_t taken = 0;
    for (const char* p = text; *p != '\0'; ++p) {
        if (taken >= max_length) {
            out += "...";
            break;
        }

        const unsigned char c = static_cast<unsigned char>(*p);
        // Printable ASCII passes through; a quote is escaped because the caller
        // wraps the result in quotes and an unescaped one would fake the end of
        // the field.
        if (c >= 0x20 && c < 0x7F && c != '\'' && c != '\\') {
            out += static_cast<char>(c);
        } else {
            out += std::format("\\x{:02X}", c);
        }
        ++taken;
    }
    return out;
}

void rv_log_emit(rv_log_level lvl, const char* tag, const char* file, int line, std::string msg) {
    if (lvl > g_threshold) return;
    std::string out = std::format("[{}] {}: {} ({}:{})\n", level_name(lvl), tag, msg, file, line);
    std::fputs(out.c_str(), stderr);
}

}  // namespace rv_3dmppc
