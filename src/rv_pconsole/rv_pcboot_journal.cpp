#include "rv_pconsole/rv_pcboot_journal.hpp"

#include <string>
#include <string_view>
#include <vector>

#include "pdklib/rv_logs/rv_logs.hpp"
#include "rv_pconsole/rv_pctexts.hpp"

namespace rv_3dmppc
{

namespace
{

// Log origin tag of this module's messages.
inline constexpr const char *rv_pcboot_log_tag = "pcboot";

// Text id of the level word printed after the component name.
inline constexpr const char *rv_pcboot_text_level_ok = "boot.level_ok";

// Text id of the level word printed after the component name for a warning.
inline constexpr const char *rv_pcboot_text_level_warning = "boot.level_warning";

// Text id of the level word printed after the component name for an error.
inline constexpr const char *rv_pcboot_text_level_error = "boot.level_error";

// Text id of the label before the found text.
inline constexpr const char *rv_pcboot_text_found = "boot.found";

// Text id of the label before the recommended text.
inline constexpr const char *rv_pcboot_text_recommend = "boot.recommend";

} // namespace

void rv_pcboot_journal::add_ok(std::string_view name)
{
    entries_.push_back({ std::string(name), rv_pcboot_level::ok, {}, {}, {}, {} });
    const std::string &stored = entries_.back().name;
    RV_LOG_INFO(rv_pcboot_log_tag, "{}: {}", rv_pdklib::rv_log_escape(stored.c_str()), rv_pctext(rv_pcboot_text_level_ok));
}

void rv_pcboot_journal::add_warning(std::string_view name, const rv_pcboot_texts &log, const rv_pcboot_texts &screen)
{
    entries_.push_back({ std::string(name),
        rv_pcboot_level::warning,
        std::string(log.found_id),
        std::string(log.recommend_id),
        std::string(screen.found_id),
        std::string(screen.recommend_id) });
    const std::string &stored = entries_.back().name;
    RV_LOG_WARN(rv_pcboot_log_tag, "{}: {}", rv_pdklib::rv_log_escape(stored.c_str()), rv_pctext(rv_pcboot_text_level_warning));
    RV_LOG_WARN(rv_pcboot_log_tag, "{}: {}", rv_pctext(rv_pcboot_text_found), rv_pctext(log.found_id));
    RV_LOG_WARN(rv_pcboot_log_tag, "{}: {}", rv_pctext(rv_pcboot_text_recommend), rv_pctext(log.recommend_id));
}

void rv_pcboot_journal::add_error(std::string_view name, const rv_pcboot_texts &log, const rv_pcboot_texts &screen)
{
    entries_.push_back({ std::string(name),
        rv_pcboot_level::error,
        std::string(log.found_id),
        std::string(log.recommend_id),
        std::string(screen.found_id),
        std::string(screen.recommend_id) });
    const std::string &stored = entries_.back().name;
    RV_LOG_ERR(rv_pcboot_log_tag, "{}: {}", rv_pdklib::rv_log_escape(stored.c_str()), rv_pctext(rv_pcboot_text_level_error));
    RV_LOG_ERR(rv_pcboot_log_tag, "{}: {}", rv_pctext(rv_pcboot_text_found), rv_pctext(log.found_id));
    RV_LOG_ERR(rv_pcboot_log_tag, "{}: {}", rv_pctext(rv_pcboot_text_recommend), rv_pctext(log.recommend_id));
}

const std::vector<rv_pcboot_entry> &rv_pcboot_journal::entries() const
{
    return entries_;
}

bool rv_pcboot_journal::has_warning() const
{
    for (const auto &entry : entries_) {
        if (entry.level == rv_pcboot_level::warning) {
            return true;
        }
    }
    return false;
}

bool rv_pcboot_journal::has_error() const
{
    for (const auto &entry : entries_) {
        if (entry.level == rv_pcboot_level::error) {
            return true;
        }
    }
    return false;
}

int64_t rv_pcboot_journal::warning_count() const
{
    int64_t count = 0;
    for (const auto &entry : entries_) {
        if (entry.level == rv_pcboot_level::warning) {
            ++count;
        }
    }
    return count;
}

} // namespace rv_3dmppc
