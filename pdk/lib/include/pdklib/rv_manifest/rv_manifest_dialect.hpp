#pragma once

#include <string>

#include "detail/rv_manifest_tree.hpp"

namespace rv_pdklib
{

// The manifest's dialect without its schema: `[section]` headers and
// `key = value` lines, values a string, an integer or an array of strings. For
// other files written the way disc.toml is (the editor's settings), so they
// share its lexer and parser instead of copying them. Which sections and keys
// mean anything is the caller's business.
//
// 0 with the tree, or 1 with every syntax error in `error`, each stamped with
// `origin` the way rv_manifest_parse stamps them.
int rv_manifest_read_tree(const std::string &text, const std::string &origin, rv_manifest_tree &tree,
    std::string &error);

} // namespace rv_pdklib
