#pragma once

#include <string>
#include <vector>

namespace rv_editor
{

// The Test Case result line: the hint while no result is recorded, else the latest one.
std::string rv_editor_case_result_line(const std::vector<std::string> &results);

} // namespace rv_editor
