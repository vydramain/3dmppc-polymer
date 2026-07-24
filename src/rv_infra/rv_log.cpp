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

void rv_log_emit(rv_log_level lvl, const char* tag, const char* file, int line, std::string msg) {
    if (lvl > g_threshold) return;
    std::string out = std::format("[{}] {}: {} ({}:{})\n", level_name(lvl), tag, msg, file, line);
    std::fputs(out.c_str(), stderr);
}

}  // namespace rv_3dmppc
