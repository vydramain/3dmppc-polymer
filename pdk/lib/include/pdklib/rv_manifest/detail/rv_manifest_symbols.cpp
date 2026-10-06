#include "rv_manifest_symbols.hpp"

#include <map>
#include <string>
#include <string_view>
#include <utility>

#include "pdk/rv_err.h"

namespace rv_pdklib
{
static int define(std::map<std::string, int> &table, const std::string &name, int line, int &first_line)
{
    const std::pair<std::map<std::string, int>::iterator, bool> placed = table.emplace(name, line);
    first_line = placed.first->second;
    return placed.second ? RV_OK : RV_ERR_INVAL;
}
const rv_manifest_section_spec *rv_manifest_symbols::lookup_section(std::string_view name) const
{
    return rv_manifest_sections_get(name);
}

const rv_manifest_key_spec *rv_manifest_symbols::lookup_key(const rv_manifest_section_spec &section,
    std::string_view name) const
{
    return rv_manifest_keys_get(section, name);
}

int rv_manifest_symbols::define_section(const std::string &name, int line, int &first_line)
{
    return define(sections_, name, line, first_line);
}

int rv_manifest_symbols::define_key(const std::string &section, const std::string &key, int line, int &first_line)
{
    return define(keys_, section + "." + key, line, first_line);
}

} // namespace rv_pdklib
