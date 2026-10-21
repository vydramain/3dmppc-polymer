#pragma once

#include <string>
#include <utility>
#include <vector>

#include "detail/rv_manifest_failer.hpp"
#include "detail/rv_manifest_lexer.hpp"
#include "detail/rv_manifest_parser.hpp"
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
inline int rv_manifest_read_tree(const std::string &text, const std::string &origin, rv_manifest_tree &tree, std::string &error)
{
    rv_manifest_failer failer;
    const std::vector<rv_manifest_token> tokens = rv_manifest_lex(text);
    rv_manifest_parser parser(tokens, failer);
    rv_manifest_tree parsed = parser.run();
    if (!failer.empty()) {
        error = failer.report(origin);
        return 1;
    }
    tree = std::move(parsed);
    return 0;
}

} // namespace rv_pdklib
