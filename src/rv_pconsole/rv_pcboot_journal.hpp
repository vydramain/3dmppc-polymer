#pragma once

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
};

// found_id and recommend_id are console text ids; empty for an ok entry.
struct rv_pcboot_entry {
    std::string name;
    rv_pcboot_level level;
    std::string found_id;
    std::string recommend_id;
};

class rv_pcboot_journal
{
public:
    void add_ok(std::string_view name);
    void add_warning(std::string_view name, std::string_view found_id, std::string_view recommend_id);

    const std::vector<rv_pcboot_entry> &entries() const;
    bool has_warning() const;

private:
    std::vector<rv_pcboot_entry> entries_;
};

} // namespace rv_3dmppc
