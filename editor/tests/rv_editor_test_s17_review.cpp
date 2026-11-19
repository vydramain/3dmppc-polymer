// S17 review findings: Export without a project keeps its whole form and both shelves (inactive, with a reason), and the
// three areas of Candidate share the free height equally.

#include <cmath>
#include <cstdio>
#include <string>

#include "app/rv_editor_shell.hpp"
#include "imgui_internal.h"
#include "rv_editor_test.hpp"
#include "rv_editor_test_imgui.hpp"
#include "rv_editor_test_s17_states.hpp"

using namespace rv_editor;
using namespace rv_editor_test;
using namespace rv_editor_test::s17;
using namespace rv_editor_test::pane_state;

namespace
{

constexpr size_t export_no_project = 0;
constexpr size_t export_disc = 1;
constexpr size_t candidate_several = 3;

void report(const std::string &bad)
{
    if (!bad.empty()) {
        std::fprintf(stderr, "  %s\n", bad.c_str());
    }
}

// Hovers the middle of `e` for 1.2 s of frames; true when a tooltip is up.
bool tooltip_over(fixture &f, const rv_editor_probe_entry &e, float w, float h)
{
    ImGui::GetIO().MousePos = ImVec2((e.x0 + e.x1) / 2.0f, (e.y0 + e.y1) / 2.0f);
    for (int i = 0; i < 12; ++i) {
        ImGui::GetIO().DeltaTime = 0.1f;
        f.draw(w, h);
    }
    const ImGuiWindow *tip = ImGui::FindWindowByName("##Tooltip_00");
    const bool up = tip != nullptr && tip->WasActive;
    ImGui::GetIO().MousePos = ImVec2(-1000.0f, -1000.0f);
    f.draw(w, h);
    f.draw(w, h);
    return up;
}

} // namespace

// R8, R24: without a project the kind dropdown, the Directory field and both shelves are where they are with a project.
RV_EDITOR_TEST(rv_editor_test_s17_export_without_a_project_keeps_its_form_and_shelves)
{
    load_texts();
    rv_editor_test_imgui gui(true);
    fixture f(gui, rv_editor_pane_kind::export_);
    const auto min = tile_of(gui, rv_editor_pane_kind::export_);
    f.states[export_disc].set();
    const auto with = settled(f, min.w, min.h);
    f.states[export_no_project].set();
    const auto none = settled(f, min.w, min.h);
    const std::vector<const char *> labels = { "##export_kind_combo",
        "##export_dest",
        rv_editor_text("pane_export.export_button"),
        rv_editor_text("pane_export.cancel_button"),
        rv_editor_text("pane_export.open_folder_button"),
        rv_editor_text("pane_export.open_browser_button"),
        rv_editor_text("pane_export.show_problems_button") };
    for (const char *label : labels) {
        const std::string d = item_difference(with, none, label);
        report(d);
        RV_EDITOR_CHECK(d.empty());
    }
    // The description box under the dropdown is there too.
    RV_EDITOR_CHECK(texts(none).size() >= 2);
    RV_EDITOR_CHECK(texts(none).size() == texts(with).size());
}

// R8: the buttons are inactive without a project and say why on hover; Export does not start anything.
RV_EDITOR_TEST(rv_editor_test_s17_export_buttons_without_a_project_are_inactive_with_a_reason)
{
    load_texts();
    rv_editor_test_imgui gui(true);
    fixture f(gui, rv_editor_pane_kind::export_);
    const auto min = tile_of(gui, rv_editor_pane_kind::export_);
    f.states[export_no_project].set();
    const auto r = settled(f, min.w, min.h);
    for (const char *key : { "pane_export.export_button",
             "pane_export.cancel_button",
             "pane_export.open_folder_button",
             "pane_export.open_browser_button",
             "pane_export.show_problems_button" }) {
        const auto *b = find_item(r, rv_editor_text(key));
        RV_EDITOR_CHECK(b != nullptr);
        if (b != nullptr) {
            const bool up = tooltip_over(f, *b, min.w, min.h);
            report(up ? "" : std::string(key) + " has no reason tooltip");
            RV_EDITOR_CHECK(up);
        }
    }
    const auto *start = find_item(r, rv_editor_text("pane_export.export_button"));
    if (start != nullptr) {
        ImGuiIO &io = ImGui::GetIO();
        io.AddMousePosEvent((start->x0 + start->x1) / 2.0f, (start->y0 + start->y1) / 2.0f);
        f.draw(min.w, min.h);
        io.AddMouseButtonEvent(0, true);
        f.draw(min.w, min.h);
        io.AddMouseButtonEvent(0, false);
        f.draw(min.w, min.h);
        f.draw(min.w, min.h);
    }
    std::error_code ec;
    RV_EDITOR_CHECK(!f.app().exporter.busy());
    RV_EDITOR_CHECK(f.app().exporter.state() == rv_editor_build_state::idle);
    RV_EDITOR_CHECK(!fs::exists(f.where / "export", ec));
}

// R20, AC21: in a roomy tile the list, the facts and the checks of Candidate get equal shares of the free height.
RV_EDITOR_TEST(rv_editor_test_s17_candidate_areas_share_the_free_height_equally)
{
    load_texts();
    rv_editor_test_imgui gui(true);
    const auto r = at_roomy(gui, rv_editor_pane_kind::candidate, candidate_several);
    const auto a = areas(r);
    RV_EDITOR_CHECK(a.size() == 3);
    if (a.size() == 3) {
        const float h0 = a[0]->y1 - a[0]->y0;
        for (const auto *e : a) {
            const float h = e->y1 - e->y0;
            report(std::fabs(h - h0) > 2.0f ? "area heights " + std::to_string(h0) + " and " + std::to_string(h) : "");
            RV_EDITOR_CHECK(std::fabs(h - h0) <= 2.0f);
        }
    }
}
