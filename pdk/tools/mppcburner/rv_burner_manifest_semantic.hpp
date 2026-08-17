#pragma once

#include "rv_burner_manifest_failer.hpp"
#include "rv_burner_manifest_tree.hpp"

namespace rv_pdktools
{

// Stage 3: judge the tree against the symbol table — section known, key belongs
// to it, value of the kind that key takes, nothing defined twice. It reports
// everything it finds and changes nothing; a tree it leaves without diagnostics
// is one the binder can walk with no checks of its own.
void rv_burner_manifest_check(const rv_burner_tree &tree, rv_burner_manifest_failer &failer);

} // namespace rv_pdktools
