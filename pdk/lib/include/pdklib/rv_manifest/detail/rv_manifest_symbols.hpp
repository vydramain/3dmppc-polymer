#pragma once

#include <map>
#include <string>
#include <string_view>

#include "rv_manifest_schema.hpp"

namespace rv_pdklib
{

// The symbol table. Its static half is the schema — which sections and keys
// exist and what each key takes. Its dynamic half is what THIS file has already
// defined and on which line, which is what lets a duplicate name the line it
// collides with instead of just saying "twice".
class rv_manifest_symbols
{
public:
	// --- static: the schema ---------------------------------------------------

	const rv_manifest_section_spec *lookup_section(std::string_view name) const;
	const rv_manifest_key_spec *lookup_key(const rv_manifest_section_spec &section,
		std::string_view name) const;

	// --- dynamic: what the file has said so far -------------------------------

	// False when the name is already defined; `first_line` then says where.
	bool define_section(const std::string &name, int line, int &first_line);
	bool define_key(const std::string &section, const std::string &key, int line, int &first_line);

private:
	std::map<std::string, int> sections_; // name → line
	std::map<std::string, int> keys_; // "section.key" → line
};

} // namespace rv_pdklib
