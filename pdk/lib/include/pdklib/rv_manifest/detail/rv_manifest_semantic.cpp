#include "rv_manifest_semantic.hpp"

#include <cstddef>
#include <iterator>
#include <string>

#include "rv_manifest_symbols.hpp"
#include "rv_manifest_value.hpp"
#include "rv_manifest_schema.hpp"
#include "rv_manifest_text.hpp"

namespace rv_pdklib
{
static void check_entry(const rv_manifest_section_spec &spec, const std::string &section,
	const rv_manifest_tree_entry &entry, rv_manifest_symbols &symbols,
	rv_manifest_failer &failer)
{
	const rv_manifest_key_spec *key = symbols.lookup_key(spec, entry.key);
	if (key == nullptr) {
		failer.fail(entry.line,
			"unknown key '" + entry.key + "' in [" + section + "]" +
				suggest_key(entry.key, spec.keys, spec.key_count));
		return;
	}

	int first_line = 0;
	if (!symbols.define_key(section, entry.key, entry.line, first_line)) {
		failer.fail(entry.line,
			"key '" + entry.key + "' in [" + section + "] is set twice — first at line " +
				std::to_string(first_line));
		return;
	}

	if (entry.value.kind != key->kind) {
		failer.fail(entry.value.line,
			"key '" + entry.key + "' in [" + section + "] takes " +
				std::string(rv_manifest_kind_name(key->kind)) + ", not " +
				std::string(rv_manifest_kind_name(entry.value.kind)));
	}
}
void rv_manifest_check(const rv_manifest_tree &tree, rv_manifest_failer &failer)
{
	rv_manifest_symbols symbols;

	for (const rv_manifest_tree_section &section : tree.sections) {
		if (failer.exhausted()) {
			return;
		}
		if (section.poisoned) {
			continue;
		}

		const rv_manifest_section_spec *spec = symbols.lookup_section(section.name);
		if (spec == nullptr) {
			// Its keys are not judged: they belong to a section that does not
			// exist, and naming each of them says nothing new.
			failer.fail(section.line,
				"unknown section '[" + section.name + "]'" +
					suggest_section(section.name, rv_manifest_sections,
						std::size(rv_manifest_sections)));
			continue;
		}

		int first_line = 0;
		if (!symbols.define_section(section.name, section.line, first_line)) {
			failer.fail(section.line,
				"section '[" + section.name + "]' appears twice — first at line " +
					std::to_string(first_line));
			continue;
		}

		for (const rv_manifest_tree_entry &entry : section.entries) {
			if (failer.exhausted()) {
				return;
			}
			check_entry(*spec, section.name, entry, symbols, failer);
		}
	}
}

} // namespace rv_pdklib
