#include "panes/rv_editor_findings_result.hpp"

#include "text/rv_editor_text.hpp"

namespace rv_editor
{

std::string rv_editor_case_result_line(const std::vector<std::string> &results)
{
    return results.empty() ? rv_editor_text("findings.no_result_yet") : results.back();
}

} // namespace rv_editor
