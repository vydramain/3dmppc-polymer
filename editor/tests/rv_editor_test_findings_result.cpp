// The Test Case result line: a hint while nothing is recorded, the latest result after that.

#include "panes/rv_editor_findings_result.hpp"
#include "rv_editor_test.hpp"
#include "text/rv_editor_text.hpp"

using namespace rv_editor;

RV_EDITOR_TEST(rv_editor_test_case_result_line_hint_when_empty)
{
    std::string error;
    RV_EDITOR_CHECK(rv_editor_text_load({}, error) == 0);
    RV_EDITOR_CHECK(rv_editor_case_result_line({}) == "No result yet: Passed, Failed or Blocked records one");
}

RV_EDITOR_TEST(rv_editor_test_case_result_line_latest_result)
{
    std::string error;
    RV_EDITOR_CHECK(rv_editor_text_load({}, error) == 0);
    RV_EDITOR_CHECK(rv_editor_case_result_line({ "case-1: Passed" }) == "case-1: Passed");
    RV_EDITOR_CHECK(rv_editor_case_result_line({ "case-1: Passed", "case-2: Failed" }) == "case-2: Failed");
}
