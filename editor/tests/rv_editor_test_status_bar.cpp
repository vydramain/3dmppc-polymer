// The status bar (R5, R17): each segment is as wide as its longest bounded text plus an equal share of the free width,
// shows its text whole in every state, and a count over 999 reads 999+; a click on Tools opens Settings; the editor
// window is never narrower than the bar's segments.

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "app/rv_editor_shell.hpp"
#include "imgui_internal.h"
#include "pdklib/rv_progress/rv_progress.hpp"
#include "rv_editor_test.hpp"
#include "rv_editor_test_fits_whole.hpp"
#include "rv_editor_test_imgui.hpp"
#include "rv_editor_test_layout_probe.hpp"
#include "session/rv_editor_session.hpp"
#include "text/rv_editor_text.hpp"
#include "ui/rv_editor_widgets.hpp"

using namespace rv_editor;
using namespace rv_editor_test;

namespace
{

constexpr rv_editor_theme theme = rv_editor_theme_olive;
constexpr float window_width = 1400.0f;
constexpr int missing_over_cap = 1000;
constexpr float rounding = 2.0f; // segment edges are rounded to whole pixels

void load_texts()
{
    std::string error;
    RV_EDITOR_CHECK(rv_editor_text_load({}, error) == 0);
}

float text_w(const std::string &s)
{
    return ImGui::CalcTextSize(s.c_str()).x;
}

// A shell with a project of this title open (or none).
void open_project(rv_editor_shell &shell, const char *title)
{
    shell.app.project.open = title != nullptr;
    shell.app.project.disc_title = title != nullptr ? title : "";
}

// The longest step name the progress protocol accepts.
std::string step_name_limit()
{
    return std::string(rv_pdklib::rv_progress_step_name_max, 'W');
}

// Build template at the limit, as the plan words it: `Build #N ` + the steps text (step 99/99: name).
std::string build_limit()
{
    return "Build #999999 step 99/99: " + step_name_limit();
}

// One frame of the bar of a shell; the probe says where the segments were.
rv_editor_status_probe shell_bar(rv_editor_test_imgui &gui, rv_editor_shell &shell, float width = window_width)
{
    rv_editor_status_probe probe;
    gui.frame(
        [&] {
            rv_editor_shell_status(shell, theme, &probe);
        },
        width);
    return probe;
}

// One frame of the bar from texts: the same widths for every state means the same edges.
rv_editor_status_probe text_bar(rv_editor_test_imgui &gui,
    const std::vector<std::string> &fields,
    const std::vector<std::string> &widest,
    float width = window_width,
    int *clicked = nullptr)
{
    std::vector<const char *> f;
    std::vector<const char *> w;
    for (size_t i = 0; i < fields.size(); ++i) {
        f.push_back(fields[i].c_str());
        w.push_back(widest[i].c_str());
    }
    rv_editor_status_probe probe;
    gui.frame(
        [&] {
            const int hit = rv_editor_status_bar(f.data(), w.data(), static_cast<int>(f.size()), theme, &probe);
            if (clicked != nullptr && hit >= 0) {
                *clicked = hit;
            }
        },
        width);
    return probe;
}

std::string same_edges(const rv_editor_status_probe &a, const rv_editor_status_probe &b)
{
    if (a.segments.size() != b.segments.size()) {
        return "segment count differs";
    }
    for (size_t i = 0; i < a.segments.size(); ++i) {
        if (a.segments[i].x0 != b.segments[i].x0 || a.segments[i].x1 != b.segments[i].x1) {
            return "segment " + std::to_string(i) + " differs";
        }
    }
    return "";
}

// The widest texts of the shell's bar as an array of strings, `shown` of them.
std::vector<std::string> widest_of(const rv_editor_app &app)
{
    const rv_editor_status_text w = rv_editor_status_text_widest(app);
    std::vector<std::string> out = { w.where, w.tools, w.build, w.runtime };
    out.resize(static_cast<size_t>(w.shown));
    return out;
}

} // namespace

// AC6: a count over 999 reads 999+; up to 999 it is the number; none is "ready".
RV_EDITOR_TEST(rv_editor_test_status_tools_text_caps_the_count)
{
    load_texts();
    RV_EDITOR_CHECK(rv_editor_status_tools_text(missing_over_cap) == "Tools: 999+ missing");
    RV_EDITOR_CHECK(rv_editor_status_tools_text(999) == "Tools: 999 missing");
    RV_EDITOR_CHECK(rv_editor_status_tools_text(1000000) == "Tools: 999+ missing");
    RV_EDITOR_CHECK(rv_editor_status_tools_text(1) == "Tools: 1 missing");
    RV_EDITOR_CHECK(rv_editor_status_tools_text(0) == "Tools: ready");
}

// R5: the segments' templates have every field at its limit; the project's is the open project's name.
RV_EDITOR_TEST(rv_editor_test_status_widest_texts_are_the_limits)
{
    load_texts();
    rv_editor_test_imgui gui;
    rv_editor_shell shell;
    open_project(shell, "My Project");
    const rv_editor_status_text w = rv_editor_status_text_widest(shell.app);
    RV_EDITOR_CHECK(w.shown == 4);
    gui.frame([&] {
        RV_EDITOR_CHECK(text_w(w.tools) >= text_w("Tools: 999+ missing"));
        RV_EDITOR_CHECK(text_w(w.build) >= text_w(build_limit()));
        float runtime = 0.0f;
        for (int i = 0; i <= static_cast<int>(rv_editor_run_state::refused); ++i) {
            const std::string t =
                std::string("Runtime: ") + rv_editor_run_state_name(static_cast<rv_editor_run_state>(i)) + ", session 999999";
            runtime = std::max(runtime, text_w(t));
        }
        RV_EDITOR_CHECK(runtime > 0.0f && text_w(w.runtime) >= runtime);
    });
    RV_EDITOR_CHECK(w.where.find("My Project") != std::string::npos);
    RV_EDITOR_CHECK(w.where.find("...") == std::string::npos);
    RV_EDITOR_CHECK(w.tools.find("...") == std::string::npos);
}

// R5: the project segment follows the name of the open project, and the no-project text has two segments.
RV_EDITOR_TEST(rv_editor_test_status_project_segment_follows_the_name)
{
    load_texts();
    rv_editor_test_imgui gui;
    rv_editor_shell shell;
    open_project(shell, "A");
    const std::string shortest = rv_editor_status_text_widest(shell.app).where;
    const std::string title = "A rather long project title 0123456789 0123456789";
    open_project(shell, title.c_str());
    const std::string longest = rv_editor_status_text_widest(shell.app).where;
    RV_EDITOR_CHECK(longest.find(title) != std::string::npos);
    const rv_editor_status_probe p = shell_bar(gui, shell);
    RV_EDITOR_CHECK(p.segments.size() == 4);
    open_project(shell, "A");
    const rv_editor_status_probe q = shell_bar(gui, shell);
    RV_EDITOR_CHECK(p.segments.size() == 4 && q.segments.size() == 4);
    if (p.segments.size() == 4 && q.segments.size() == 4) {
        gui.frame([&] {
            RV_EDITOR_CHECK(text_w(longest) > text_w(shortest));
        });
        RV_EDITOR_CHECK(p.segments[0].x1 - p.segments[0].x0 > q.segments[0].x1 - q.segments[0].x0);
        RV_EDITOR_CHECK(p.segments[0].shown.find(title) != std::string::npos);
    }
    open_project(shell, nullptr);
    RV_EDITOR_CHECK(rv_editor_status_text_widest(shell.app).shown == 2);
    gui.frame([&] {
        const std::string none = rv_editor_status_text_widest(shell.app).where;
        RV_EDITOR_CHECK(text_w(none) >= text_w(rv_editor_text("shell_status.no_project")));
    });
}

// R17: a segment is as wide as its longest text plus the same free share as every other segment.
RV_EDITOR_TEST(rv_editor_test_status_segments_are_text_plus_equal_share)
{
    load_texts();
    rv_editor_test_imgui gui;
    const std::vector<std::string> widest = { "My Project",
        "Tools: 999+ missing",
        build_limit(),
        "Runtime: running, session 999999" };
    const rv_editor_status_probe p = text_bar(gui, widest, widest);
    RV_EDITOR_CHECK(p.segments.size() == 4);
    if (p.segments.size() != 4) {
        return;
    }
    float share0 = 0.0f;
    gui.frame([&] {
        for (size_t i = 0; i < 4; ++i) {
            const float free_share = (p.segments[i].x1 - p.segments[i].x0) - text_w(widest[i]);
            RV_EDITOR_CHECK(free_share > 0.0f);
            if (i == 0) {
                share0 = free_share;
            }
            RV_EDITOR_CHECK(std::fabs(free_share - share0) <= rounding);
        }
    });
}

// AC6: build #999999 and session #999999 change no segment's width, nor does any other state, at one window width.
RV_EDITOR_TEST(rv_editor_test_status_segment_widths_same_in_every_state)
{
    load_texts();
    rv_editor_test_imgui gui;
    rv_editor_shell shell;
    open_project(shell, "My Project");
    const std::vector<std::string> widest = widest_of(shell.app);
    RV_EDITOR_CHECK(widest.size() == 4);
    const std::vector<std::vector<std::string>> states = {
        { "My Project", "Tools: ready", "Build: not built", "Runtime: stopped" },
        { "My Project", "Tools: ready", "Build #3 step 2/5: compile", "Runtime: stopped" },
        { "My Project", "Tools: ready", "Build #3 step 2/5: compile", "Runtime: starting, session 4" },
        { "My Project", "Tools: 4 missing", "Build #3", "Runtime: crashed, session 4" },
        { "My Project", "Tools: 999+ missing", build_limit(), "Runtime: running, session 999999" },
        { "My Project", "Tools: 999+ missing", "Build #999999", "Runtime: disconnected, session 999999" },
    };
    const rv_editor_status_probe first = text_bar(gui, states[0], widest);
    RV_EDITOR_CHECK(first.segments.size() == 4);
    for (const auto &state : states) {
        const rv_editor_status_probe p = text_bar(gui, state, widest);
        RV_EDITOR_CHECK(same_edges(first, p).empty());
        for (size_t i = 0; i < p.segments.size(); ++i) {
            RV_EDITOR_CHECK(p.segments[i].shown == state[i]);
        }
    }
    // No project: two segments, and they too keep their edges.
    rv_editor_shell none;
    const std::vector<std::string> none_widest = widest_of(none.app);
    RV_EDITOR_CHECK(none_widest.size() == 2);
    const rv_editor_status_probe a = text_bar(gui, { "No project", "Tools: ready" }, none_widest);
    const rv_editor_status_probe b = text_bar(gui, { "No project", "Tools: 999+ missing" }, none_widest);
    RV_EDITOR_CHECK(a.segments.size() == 2 && same_edges(a, b).empty());
}

// AC18: the real texts of the app (no project, project, tools missing) fit their segments whole.
RV_EDITOR_TEST(rv_editor_test_status_shell_texts_are_whole_in_their_segments)
{
    load_texts();
    rv_editor_test_imgui gui;
    for (const char *title : { static_cast<const char *>(nullptr), "My Project" }) {
        for (const bool missing : { false, true }) {
            rv_editor_shell shell;
            open_project(shell, title);
            if (missing) {
                shell.app.tools.console.problem = "not found";
                shell.app.tools.burner.problem = "not found";
            }
            const rv_editor_status_probe p = shell_bar(gui, shell);
            RV_EDITOR_CHECK(p.segments.size() == (title != nullptr ? 4u : 2u));
            // The no-project text ends in "..." of its own (a menu path), so the text is compared, not searched.
            const rv_editor_status_text whole = rv_editor_status_text_make(shell.app);
            const std::vector<std::string> expected = { whole.where, whole.tools, whole.build, whole.runtime };
            gui.frame([&] {
                for (size_t i = 0; i < p.segments.size() && i < expected.size(); ++i) {
                    const auto &s = p.segments[i];
                    RV_EDITOR_CHECK(s.shown == expected[i]);
                    RV_EDITOR_CHECK(s.tooltip.empty());
                    RV_EDITOR_CHECK(text_w(s.shown) <= s.x1 - s.x0);
                }
            });
        }
    }
}

// fits_whole: the bar's segments are items named by their text, inside the window, none cut.
RV_EDITOR_TEST(rv_editor_test_status_bar_fits_whole)
{
    load_texts();
    rv_editor_test_imgui gui;
    rv_editor_shell shell;
    open_project(shell, "My Project");
    const std::vector<std::string> fields = { "My Project",
        "Tools: 999+ missing",
        build_limit(),
        "Runtime: running, session 999999" };
    for (const auto &widest : { widest_of(shell.app), fields }) {
        std::vector<const char *> f;
        std::vector<const char *> w;
        for (size_t i = 0; i < fields.size(); ++i) {
            f.push_back(fields[i].c_str());
            w.push_back(widest[i].c_str());
        }
        rv_editor_probe_draw(gui, window_width, [&](rv_editor_probe_record &) {
            rv_editor_status_bar(f.data(), w.data(), 4, theme);
        });
        const rv_editor_probe_record record = rv_editor_probe_draw(gui, window_width, [&](rv_editor_probe_record &) {
            rv_editor_status_bar(f.data(), w.data(), 4, theme);
        });
        RV_EDITOR_CHECK(rv_editor_fits_whole(record, ImRect(0, 0, window_width, 300)).empty());
        // Each segment is an item labelled with its text, so fits_whole measures that text against the segment.
        int named = 0;
        for (const auto &e : record.entries) {
            for (const auto &text : fields) {
                named += e.label.substr(0, e.label.find("##")) == text ? 1 : 0;
            }
        }
        RV_EDITOR_CHECK(named == 4);
    }
}

// AC6: a click on a segment is reported by index; a click on the bar's free space is not.
RV_EDITOR_TEST(rv_editor_test_status_bar_reports_the_clicked_segment)
{
    load_texts();
    rv_editor_test_imgui gui;
    const std::vector<std::string> f = { "My Project", "Tools: 2 missing", "Build: not built", "Runtime: stopped" };
    const rv_editor_status_probe p = text_bar(gui, f, f);
    RV_EDITOR_CHECK(p.segments.size() == 4);
    if (p.segments.size() != 4) {
        return;
    }
    int hit = -1;
    ImGui::GetIO().AddMousePosEvent((p.segments[1].x0 + p.segments[1].x1) / 2.0f, 8.0f);
    text_bar(gui, f, f, window_width, &hit);
    ImGui::GetIO().AddMouseButtonEvent(0, true);
    text_bar(gui, f, f, window_width, &hit);
    ImGui::GetIO().AddMouseButtonEvent(0, false);
    text_bar(gui, f, f, window_width, &hit);
    text_bar(gui, f, f, window_width, &hit);
    RV_EDITOR_CHECK(hit == 1);
}

// AC6: clicking the Tools segment opens Settings, with a project (a tab) and without one (the start page).
RV_EDITOR_TEST(rv_editor_test_status_click_on_tools_opens_settings)
{
    load_texts();
    for (const bool project : { false, true }) {
        rv_editor_test_imgui gui;
        rv_editor_shell shell;
        open_project(shell, project ? "My Project" : nullptr);
        shell.app.tools.console.problem = "not found";
        const rv_editor_status_probe p = shell_bar(gui, shell);
        RV_EDITOR_CHECK(p.segments.size() == (project ? 4u : 2u));
        if (p.segments.size() < 2) {
            return;
        }
        ImGui::GetIO().AddMousePosEvent((p.segments[1].x0 + p.segments[1].x1) / 2.0f, 8.0f);
        shell_bar(gui, shell);
        ImGui::GetIO().AddMouseButtonEvent(0, true);
        shell_bar(gui, shell);
        ImGui::GetIO().AddMouseButtonEvent(0, false);
        shell_bar(gui, shell);
        shell_bar(gui, shell);
        if (!project) {
            RV_EDITOR_CHECK(shell.start_page == rv_editor_start_page::settings);
            continue;
        }
        bool shown = false;
        for (const rv_editor_tile_node &node : shell.ws.layout.nodes) {
            if (node.kind == rv_editor_tile_kind::leaf && !node.leaf.tabs.empty() &&
                shell.ws.panes.panes[node.leaf.tabs[node.leaf.active]].kind == rv_editor_pane_kind::settings) {
                shown = true;
            }
        }
        RV_EDITOR_CHECK(shown);
    }
}

// A click on the project segment does not open Settings.
RV_EDITOR_TEST(rv_editor_test_status_click_on_project_opens_nothing)
{
    load_texts();
    rv_editor_test_imgui gui;
    rv_editor_shell shell;
    open_project(shell, nullptr);
    const rv_editor_status_probe p = shell_bar(gui, shell);
    RV_EDITOR_CHECK(p.segments.size() == 2);
    if (p.segments.size() != 2) {
        return;
    }
    ImGui::GetIO().AddMousePosEvent((p.segments[0].x0 + p.segments[0].x1) / 2.0f, 8.0f);
    shell_bar(gui, shell);
    ImGui::GetIO().AddMouseButtonEvent(0, true);
    shell_bar(gui, shell);
    ImGui::GetIO().AddMouseButtonEvent(0, false);
    shell_bar(gui, shell);
    RV_EDITOR_CHECK(shell.start_page != rv_editor_start_page::settings);
}

// AC18: the bar's least width holds the widest texts of its segments whole, and a bar of exactly that width shows them.
RV_EDITOR_TEST(rv_editor_test_status_bar_min_width_holds_the_segments)
{
    load_texts();
    rv_editor_test_imgui gui;
    const std::vector<std::string> widest = { "My Project",
        "Tools: 999+ missing",
        build_limit(),
        "Runtime: running, session 999999" };
    std::vector<const char *> w;
    for (const auto &s : widest) {
        w.push_back(s.c_str());
    }
    float sum = 0.0f;
    float min = 0.0f;
    gui.frame([&] {
        for (const auto &s : widest) {
            sum += text_w(s);
        }
        min = rv_editor_status_bar_min_width(w.data(), 4, theme);
    });
    RV_EDITOR_CHECK(min > sum); // paddings and gaps come on top of the texts
    if (min <= sum) {
        return;
    }
    const rv_editor_status_probe p = text_bar(gui, widest, widest, min);
    RV_EDITOR_CHECK(p.segments.size() == 4);
    gui.frame([&] {
        for (size_t i = 0; i < p.segments.size(); ++i) {
            RV_EDITOR_CHECK(text_w(widest[i]) <= p.segments[i].x1 - p.segments[i].x0);
        }
    });
    RV_EDITOR_CHECK(p.segments.size() == 4 && p.segments.back().x1 <= min);
}

// AC18: the editor window's least width is the status bar's least width plus its margins, but never under 1280 x scale.
RV_EDITOR_TEST(rv_editor_test_window_min_width_covers_the_status_bar)
{
    RV_EDITOR_CHECK(rv_editor_window_min_width_for(1.0f, 900.0f) == 1280);
    RV_EDITOR_CHECK(rv_editor_window_min_width_for(1.5f, 900.0f) == 1920);
    RV_EDITOR_CHECK(rv_editor_window_min_width_for(1.0f, 1500.0f) == 1500);
    RV_EDITOR_CHECK(rv_editor_window_min_width_for(1.0f, 1500.2f) == 1501);
    RV_EDITOR_CHECK(rv_editor_window_min_width_for(2.0f, 5000.0f) == 5000);
}

// AC18: for the longest project name the shell's bar minimum is more than its texts and the window takes it in.
RV_EDITOR_TEST(rv_editor_test_shell_status_min_width_follows_the_project)
{
    load_texts();
    rv_editor_test_imgui gui;
    rv_editor_shell shell;
    open_project(shell, "A");
    float small = 0.0f;
    float big = 0.0f;
    float texts = 0.0f;
    const std::string title(120, 'W');
    gui.frame([&] {
        small = rv_editor_shell_status_min_width(shell.app, theme);
        open_project(shell, title.c_str());
        big = rv_editor_shell_status_min_width(shell.app, theme);
        for (const auto &t : widest_of(shell.app)) {
            texts += text_w(t);
        }
    });
    RV_EDITOR_CHECK(big > texts);
    RV_EDITOR_CHECK(big > small);
    RV_EDITOR_CHECK(rv_editor_window_min_width_for(1.0f, big) >= static_cast<int>(std::ceil(big)));
    RV_EDITOR_CHECK(rv_editor_window_min_width_for(1.0f, big) >= window_min_width);
}
