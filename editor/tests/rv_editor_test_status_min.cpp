// The status bar's window minimum (AC18, R17): it counts the Build and Runtime segments even before a project is open,
// grows with the open project's name, is measured with or without a frame, and is recomputed when the project changes.

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "app/rv_editor_shell.hpp"
#include "pdklib/rv_progress/rv_progress.hpp"
#include "rv_editor_test.hpp"
#include "rv_editor_test_imgui.hpp"
#include "session/rv_editor_session.hpp"
#include "text/rv_editor_text.hpp"
#include "ui/rv_editor_widgets.hpp"

using namespace rv_editor;
using namespace rv_editor_test;

namespace
{

constexpr rv_editor_theme theme = rv_editor_theme_olive;
constexpr float pixel_slack = 1.0f;

void load_texts()
{
    std::string error;
    RV_EDITOR_CHECK(rv_editor_text_load({}, error) == 0);
}

void open_project(rv_editor_shell &shell, const char *title)
{
    shell.app.project.open = title != nullptr;
    shell.app.project.disc_title = title != nullptr ? title : "";
}

float text_w(const std::string &s)
{
    return ImGui::CalcTextSize(s.c_str()).x;
}

// The widest Runtime template by pixels over every state.
std::string runtime_widest_by_pixels()
{
    std::string best;
    for (int i = 0; i <= static_cast<int>(rv_editor_run_state::refused); ++i) {
        const std::string t =
            std::string("Runtime: ") + rv_editor_run_state_name(static_cast<rv_editor_run_state>(i)) + ", session 999999";
        best = text_w(t) > text_w(best) ? t : best;
    }
    return best;
}

} // namespace

// AC18: the minimum without a project already holds all four segments, so opening a project cannot break it.
RV_EDITOR_TEST(rv_editor_test_status_min_without_project_counts_all_four_segments)
{
    load_texts();
    rv_editor_test_imgui gui(true);
    rv_editor_shell shell;
    open_project(shell, nullptr);
    float none = 0.0f;
    float expected = 0.0f;
    float with_a = 0.0f;
    gui.frame([&] {
        none = rv_editor_shell_status_min_width(shell.app, theme);
        const std::string where = rv_editor_text("shell_status.no_project");
        const std::string build = "Build #999999 step 99/99: " + std::string(rv_pdklib::rv_progress_step_name_max, 'W');
        const std::string runtime = runtime_widest_by_pixels();
        const char *const w[] = { where.c_str(), "Tools: 999+ missing", build.c_str(), runtime.c_str() };
        expected = rv_editor_status_bar_min_width(w, 4, theme);
        open_project(shell, "A");
        with_a = rv_editor_shell_status_min_width(shell.app, theme);
    });
    RV_EDITOR_CHECK(none >= expected - pixel_slack);
    RV_EDITOR_CHECK(none >= with_a - pixel_slack);
}

// AC18: with a project open the minimum grows with the project's name.
RV_EDITOR_TEST(rv_editor_test_status_min_grows_with_the_project_name)
{
    load_texts();
    rv_editor_test_imgui gui(true);
    rv_editor_shell shell;
    const std::string long_name(200, 'W');
    float none = 0.0f;
    float shorter = 0.0f;
    float longer = 0.0f;
    gui.frame([&] {
        none = rv_editor_shell_status_min_width(shell.app, theme);
        open_project(shell, "A");
        shorter = rv_editor_shell_status_min_width(shell.app, theme);
        open_project(shell, long_name.c_str());
        longer = rv_editor_shell_status_min_width(shell.app, theme);
    });
    RV_EDITOR_CHECK(longer > shorter);
    RV_EDITOR_CHECK(longer > none);
}

// AC18: the minimum is the same outside a frame (main.cpp measures between frames) as inside one.
RV_EDITOR_TEST(rv_editor_test_status_min_is_measured_without_a_frame)
{
    load_texts();
    rv_editor_test_imgui gui(true);
    rv_editor_shell shell;
    open_project(shell, "My Project");
    float inside = 0.0f;
    gui.frame([&] {
        inside = rv_editor_shell_status_min_width(shell.app, theme);
    });
    const float outside = rv_editor_shell_status_min_width(shell.app, theme);
    RV_EDITOR_CHECK(std::abs(outside - inside) <= pixel_slack);
}

// R5: the Runtime template is the widest by pixels among all states, not by letters.
RV_EDITOR_TEST(rv_editor_test_status_runtime_widest_is_by_pixels)
{
    load_texts();
    rv_editor_test_imgui gui(true);
    rv_editor_shell shell;
    gui.frame([&] {
        const std::string runtime = rv_editor_status_text_widest(shell.app).runtime;
        RV_EDITOR_CHECK(text_w(runtime) >= text_w(runtime_widest_by_pixels()) - pixel_slack);
    });
}

// AC18: the window minimum is reported as changed when the project opens, closes or is renamed, not otherwise.
RV_EDITOR_TEST(rv_editor_test_status_window_min_changed_follows_the_project)
{
    load_texts();
    rv_editor_test_imgui gui(true);
    gui.frame([] {});
    rv_editor_shell shell;
    const std::string wide(200, 'W');
    const std::string wider(260, 'W');
    int none = 0;
    int a = 0;
    int b = 0;
    int back = 0;
    int same = 0;
    RV_EDITOR_CHECK(rv_editor_shell_window_min_changed(shell, theme, none)); // the first call has no earlier value
    RV_EDITOR_CHECK(none >= window_min_width);
    RV_EDITOR_CHECK(!rv_editor_shell_window_min_changed(shell, theme, same));
    RV_EDITOR_CHECK(same == none);
    open_project(shell, wide.c_str());
    RV_EDITOR_CHECK(rv_editor_shell_window_min_changed(shell, theme, a));
    RV_EDITOR_CHECK(a > none);
    RV_EDITOR_CHECK(!rv_editor_shell_window_min_changed(shell, theme, same));
    RV_EDITOR_CHECK(same == a);
    open_project(shell, wider.c_str());
    RV_EDITOR_CHECK(rv_editor_shell_window_min_changed(shell, theme, b));
    RV_EDITOR_CHECK(b > a);
    open_project(shell, nullptr);
    RV_EDITOR_CHECK(rv_editor_shell_window_min_changed(shell, theme, back));
    RV_EDITOR_CHECK(back == none);
}
