// The front two stages of rv_manifest_parse, for files with no manifest schema.

#include "rv_manifest_dialect.hpp"

#include <utility>
#include <vector>

#include "detail/rv_manifest_failer.hpp"
#include "detail/rv_manifest_lexer.hpp"
#include "detail/rv_manifest_parser.hpp"

namespace rv_pdklib
{

int rv_manifest_read_tree(const std::string &text, const std::string &origin, rv_manifest_tree &tree,
    std::string &error)
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
