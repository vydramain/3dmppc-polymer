// The layout probe's own tests: equal draws, a moved item, a grown child window, a resize round trip.

#include <string>

#include "imgui.h"
#include "rv_editor_test.hpp"
#include "rv_editor_test_layout_probe.hpp"

using namespace rv_editor_test;

namespace
{

bool has(const std::string &text, const char *part)
{
    return text.find(part) != std::string::npos;
}

void simple_body(rv_editor_probe_record &, bool extra_line)
{
    if (extra_line) {
        ImGui::TextUnformatted("a conditional line");
    }
    ImGui::Button("Run");
}

} // namespace

RV_EDITOR_TEST(rv_editor_test_layout_probe_equal_draws)
{
    rv_editor_test_imgui gui;
    const auto a = rv_editor_probe_draw(gui, 400, [](auto &p) {
        simple_body(p, false);
    });
    const auto b = rv_editor_probe_draw(gui, 400, [](auto &p) {
        simple_body(p, false);
    });
    RV_EDITOR_CHECK(!a.entries.empty());
    RV_EDITOR_CHECK(a.content_height > 0);
    RV_EDITOR_CHECK(rv_editor_probe_compare(a, b).empty());
}

RV_EDITOR_TEST(rv_editor_test_layout_probe_reports_a_moved_item)
{
    rv_editor_test_imgui gui;
    const auto a = rv_editor_probe_draw(gui, 400, [](auto &p) {
        simple_body(p, false);
    });
    const auto b = rv_editor_probe_draw(gui, 400, [](auto &p) {
        simple_body(p, true);
    });
    const std::string diff = rv_editor_probe_compare(a, b);
    RV_EDITOR_CHECK(has(diff, "Run"));
}

RV_EDITOR_TEST(rv_editor_test_layout_probe_reports_a_grown_child_window)
{
    rv_editor_test_imgui gui;
    const auto draw = [&](float height) {
        return rv_editor_probe_draw(gui, 400, [height](auto &) {
            ImGui::BeginChild("log", ImVec2(0, height), ImGuiChildFlags_Borders);
            ImGui::TextUnformatted("line");
            ImGui::EndChild();
        });
    };
    const auto a = draw(100);
    const auto b = draw(140);
    RV_EDITOR_CHECK(has(rv_editor_probe_compare(a, b), "log"));
}

RV_EDITOR_TEST(rv_editor_test_layout_probe_resize_round_trip_is_equal_for_a_flowing_layout)
{
    rv_editor_test_imgui gui;
    const auto r = rv_editor_probe_resize(gui, 400, 250, [](auto &) {
        ImGui::TextWrapped("a long flowing paragraph that wraps onto several lines when the window is narrow enough");
        ImGui::Button("Run");
    });
    RV_EDITOR_CHECK(!r.first.entries.empty());
    RV_EDITOR_CHECK(!r.third.entries.empty());
    RV_EDITOR_CHECK(r.difference.empty());
}
