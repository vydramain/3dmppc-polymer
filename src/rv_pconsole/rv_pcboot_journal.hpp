#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

// Boot journal: component results in the order they were added; each add is logged at once.
// Main thread only.
namespace rv_3dmppc
{

enum class rv_pcboot_level {
    ok,
    warning,
    error,
};

// A warning's or an error's two console text ids: the found line and the recommended line.
struct rv_pcboot_texts {
    std::string_view found_id;
    std::string_view recommend_id;
};

// Console text ids, empty for an ok entry: found_id and recommend_id are the full texts
// the log prints; screen_found_id and screen_recommend_id are the short texts for the screen.
struct rv_pcboot_entry {
    std::string name;
    rv_pcboot_level level;
    std::string found_id;
    std::string recommend_id;
    std::string screen_found_id;
    std::string screen_recommend_id;
};

class rv_pcboot_journal
{
public:
    void add_ok(std::string_view name);
    void add_warning(std::string_view name, const rv_pcboot_texts &log, const rv_pcboot_texts &screen);
    // A component that did not come up.
    void add_error(std::string_view name, const rv_pcboot_texts &log, const rv_pcboot_texts &screen);

    const std::vector<rv_pcboot_entry> &entries() const;
    bool has_warning() const;
    bool has_error() const;
    int64_t warning_count() const;

private:
    std::vector<rv_pcboot_entry> entries_;
};

} // namespace rv_3dmppc
