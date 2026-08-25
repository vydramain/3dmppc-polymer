#include "rv_burner_manifest_symbols.hpp"

#include <map>
#include <string>
#include <string_view>
#include <utility>

namespace rv_pdktools
{
static bool define(std::map<std::string, int> &table, const std::string &name, int line, int &first_line)
{
	const std::pair<std::map<std::string, int>::iterator, bool> placed =
		table.emplace(name, line);
	first_line = placed.first->second;
	return placed.second;
}
const rv_burner_section_spec *rv_burner_manifest_symbols::lookup_section(
	std::string_view name) const
{
	return rv_burner_sections_get(name);
}

const rv_burner_key_spec *rv_burner_manifest_symbols::lookup_key(
	const rv_burner_section_spec &section, std::string_view name) const
{
	return rv_burner_keys_get(section, name);
}

bool rv_burner_manifest_symbols::define_section(const std::string &name, int line, int &first_line)
{
	return define(sections_, name, line, first_line);
}

bool rv_burner_manifest_symbols::define_key(const std::string &section, const std::string &key,
	int line, int &first_line)
{
	return define(keys_, section + "." + key, line, first_line);
}

} // namespace rv_pdktools
