#pragma once

#include "rv_manifest_failer.hpp"
#include "rv_manifest_tree.hpp"

namespace rv_pdklib
{

// Stage 3: judge the tree against the symbol table — section known, key belongs
// to it, value of the kind that key takes, nothing defined twice. It reports
// everything it finds and changes nothing; a tree it leaves without diagnostics
// is one the binder can walk with no checks of its own.
void rv_manifest_check(const rv_manifest_tree &tree, rv_manifest_failer &failer);

} // namespace rv_pdklib
