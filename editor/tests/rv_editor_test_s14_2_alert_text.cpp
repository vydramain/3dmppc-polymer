// S14.2 (live run): the strip of a failed build in the Inspector Report says its cause in the editor's own words. The
// burner's text for an error-findings refusal says "(listed above)", right for its own console and wrong under a table in
// a pane of the editor: the strip says how many findings there are instead.

#include <cctype>
#include <string>
#include <vector>

#include "app/rv_editor_app.hpp"
#include "panes/rv_editor_panes.hpp"
#include "rv_editor_test.hpp"
#include "text/rv_editor_text.hpp"

using namespace rv_editor;

namespace
{

// What pdk/tools/mppcburner/rv_burner_sources/rv_burner_sources_check.cpp gives as the reason of that refusal.
const std::string burner_reason = "sources have error findings (listed above), so the build does not accept them";

// The numbers (runs of digits) in `text`.
std::vector<std::string> numbers_in(const std::string &text)
{
    std::vector<std::string> out;
    for (size_t i = 0; i < text.size(); ++i) {
        if (std::isdigit(static_cast<unsigned char>(text[i])) != 0) {
            size_t end = i;
            while (end < text.size() && std::isdigit(static_cast<unsigned char>(text[end])) != 0) {
                ++end;
            }
            out.push_back(text.substr(i, end - i));
            i = end;
        }
    }
    return out;
}

bool has_number(const std::string &text, const std::string &number)
{
    for (const std::string &n : numbers_in(text)) {
        if (n == number) {
            return true;
        }
    }
    return false;
}

} // namespace

// AC6: the burner's refusal for error findings: the strip has no "above", names the build and the number of error
// findings (3 here; the warning and the other texts do not count), and does not repeat the burner's sentence.
RV_EDITOR_TEST(rv_editor_test_s14_2_strip_of_error_findings_says_how_many_and_not_above)
{
    std::string error;
    RV_EDITOR_CHECK(rv_editor_text_load({}, error) == 0);
    rv_editor_app app;
    app.project.open = true;
    for (int i = 0; i < 3; ++i) {
        app.problems.push_back({ "/p/scenes/a.scene.toml", 10 + i, 0, true, "unknown key [scene-unknown-key]", "inspector" });
    }
    app.problems.push_back({ "/p/scenes/a.scene.toml", 40, 0, false, "odd value [scene-value]", "inspector" });
    app.build_alert = { rv_editor_build_alert_kind::failed, burner_reason };
    const std::string text = rv_editor_problems_alert_text(app);
    RV_EDITOR_CHECK(text.find("above") == std::string::npos);
    RV_EDITOR_CHECK(text.find("listed") == std::string::npos);
    RV_EDITOR_CHECK(text.find("failed") != std::string::npos);
    RV_EDITOR_CHECK(has_number(text, "3"));
    RV_EDITOR_CHECK(!has_number(text, "4"));
}
