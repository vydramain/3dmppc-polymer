#pragma once

#include <format>
#include <string>

namespace rv_3dmppc {

enum rv_log_level : int {
    RV_LOG_LEVEL_EMERG = 0,
    RV_LOG_LEVEL_ERR = 1,
    RV_LOG_LEVEL_WARN = 2,
    RV_LOG_LEVEL_INFO = 3,
    RV_LOG_LEVEL_DBG = 4,
};

void rv_log_emit(rv_log_level lvl, const char* tag, const char* file, int line, std::string msg);

#define RV_LOG(lvl, tag, ...)                                                    \
    do {                                                                         \
        rv_log_emit((lvl), (tag), __FILE__, __LINE__, std::format(__VA_ARGS__)); \
    } while (0)

#define RV_LOG_ERR(tag, ...) RV_LOG(RV_LOG_LEVEL_ERR, (tag), __VA_ARGS__)
#define RV_LOG_WARN(tag, ...) RV_LOG(RV_LOG_LEVEL_WARN, (tag), __VA_ARGS__)
#define RV_LOG_INFO(tag, ...) RV_LOG(RV_LOG_LEVEL_INFO, (tag), __VA_ARGS__)
#define RV_LOG_DBG(tag, ...) RV_LOG(RV_LOG_LEVEL_DBG, (tag), __VA_ARGS__)

}  // namespace rv_3dmppc
