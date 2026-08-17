#pragma once

#include <string>
#include <vector>

#include "rv_burner_manifest_value.hpp"

namespace rv_pdktools
{

// The document as the parser leaves it: sections in file order, each with its
// entries in file order. A key here is the word that was written, not a field
// of rv_manifest — nothing at this level knows the structure exists.

struct rv_burner_tree_entry {
	std::string key;
	int line = 0; // the key's line; entry.value.line is the value's
	rv_burner_mvalue value;
};

struct rv_burner_tree_section {
	std::string name;
	int line = 0;
	// The header was broken, or these entries appeared before any header. The
	// parser has already reported it; semantic analysis skips the section so one
	// mistake is not counted twice.
	bool poisoned = false;
	std::vector<rv_burner_tree_entry> entries;
};

struct rv_burner_tree {
	std::vector<rv_burner_tree_section> sections;
};

} // namespace rv_pdktools
