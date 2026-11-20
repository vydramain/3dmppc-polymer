// S15.1, R24, R26: Run Config, Findings and Test Case with no project open. Every action button is disabled with the
// reason in its tooltip, a press does nothing (no request, no file, no profile change), the fields take no input and
// the frames stay where they are with a project.

#include <chrono>
#include <set>
#include <string>

#include "app/rv_editor_shell.hpp"
#include "imgui_internal.h"
#include "rv_editor_test.hpp"
#include "rv_editor_test_imgui.hpp"
#include "rv_editor_test_s15.hpp"
#include "rv_editor_test_strip.hpp"
#include "ui/rv_editor_widgets.hpp"

using namespace rv_editor;
using namespace rv_editor_test;
using namespace rv_editor_test::s15;

namespace
{

constexpr float roomy_w = 900.0f;
constexpr float roomy_h = 640.0f;
constexpr size_t no_project_state = 0;
constexpr size_t with_project_state = 1;

struct button {
    std::string name;
    const rv_editor_probe_entry *entry;
};

// The record of the state `index`, with the pane drawn a few frames.
rv_editor_probe_record show(fixture &f, size_t index, float w = roomy_w, float h = roomy_h)
{
    f.states[index]();
    return f.settled(w, h);
}

const rv_editor_probe_entry *find_button(const rv_editor_probe_record &r, const std::string &text, const char *id)
{
    const auto *e = text.empty() ? nullptr : find_text(r, text);
    return e != nullptr ? e : (id != nullptr ? find_id(r, id) : nullptr);
}

// The reasons a disabled action may give for a missing project: the plan names `pane_files.no_project_open` or an analogue.
std::vector<std::string> reasons()
{
    return { rv_editor_text("pane_files.no_project_open"),
        rv_editor_text("panes.no_project_open_message"),
        rv_editor_text("panes.no_project_open_full") };
}

// True when the tooltip window shown now draws the text of one of the reasons, glyph for glyph (a size would match any
// text of the same length). A glyph is a quad of four vertices whose first carries the glyph's uv; the border and the
// background may sit before or after the text, so the search starts at any vertex.
bool tooltip_is_a_reason(rv_editor_test_imgui &gui)
{
    const ImGuiWindow *tip = ImGui::FindWindowByName("##Tooltip_00");
    if (tip == nullptr || !tip->WasActive) {
        return false;
    }
    bool match = false;
    gui.frame([&] {
        std::vector<ImVec2> v;
        for (int i = 0; i < tip->DrawList->VtxBuffer.Size; ++i) {
            v.push_back(tip->DrawList->VtxBuffer[i].uv);
        }
        for (const std::string &reason : reasons()) {
            std::vector<ImVec2> want;
            for (const char c : reason) {
                if (c != ' ') {
                    const ImFontGlyph *g = ImGui::GetFontBaked()->FindGlyph(static_cast<ImWchar>(c));
                    want.emplace_back(g->U0, g->V0);
                }
            }
            for (size_t i = 0; !want.empty() && !match && i + 4 * (want.size() - 1) < v.size(); ++i) {
                bool same = true;
                for (size_t k = 0; same && k < want.size(); ++k) {
                    const ImVec2 &p = v[i + 4 * k];
                    same = std::abs(want[k].x - p.x) < 1e-6f && std::abs(want[k].y - p.y) < 1e-6f;
                }
                match = same;
            }
        }
    });
    return match;
}

// The pointer over `e` for longer than the tooltip delay.
void hover(fixture &f, const rv_editor_probe_entry &e)
{
    ImGuiIO &io = ImGui::GetIO();
    io.AddMousePosEvent((e.x0 + e.x1) / 2.0f, (e.y0 + e.y1) / 2.0f);
    for (int i = 0; i < 12; ++i) {
        io.DeltaTime = 0.1f;
        f.draw(roomy_w, roomy_h);
    }
}

std::set<std::string> listing(const fs::path &dir, bool deep)
{
    std::set<std::string> out;
    std::error_code ec;
    if (deep) {
        for (const auto &e : fs::recursive_directory_iterator(dir, ec)) {
            out.insert(e.path().string());
        }
    } else {
        for (const auto &e : fs::directory_iterator(dir, ec)) {
            out.insert(e.path().string());
        }
    }
    return out;
}

constexpr rv_editor_pane_request_kind request_kinds[] = {
    rv_editor_pane_request_kind::profile_apply,
    rv_editor_pane_request_kind::profile_apply_restart,
    rv_editor_pane_request_kind::profile_add,
    rv_editor_pane_request_kind::profile_delete,
    rv_editor_pane_request_kind::save_finding,
    rv_editor_pane_request_kind::capture_frame,
    rv_editor_pane_request_kind::case_new,
    rv_editor_pane_request_kind::case_result,
};

// The probe record without what is inside content areas.
rv_editor_probe_record frames_only(const rv_editor_probe_record &r)
{
    rv_editor_probe_record out;
    out.content_height = r.content_height;
    for (const auto &e : r.entries) {
        if (!e.in_scroll) {
            out.entries.push_back(e);
        }
    }
    return out;
}

// Each button in the no-project state: disabled with a reason on hover; a press changes nothing.
void check_buttons(rv_editor_pane_kind kind, const std::vector<std::pair<const char *, const char *>> &keys_and_ids)
{
    load_texts();
    rv_editor_test_imgui gui(true);
    fixture f(gui, kind);
    const std::set<std::string> cwd_before = listing(fs::current_path(), false);
    for (const auto &[key, id] : keys_and_ids) {
        const std::string text = key != nullptr ? rv_editor_text(key) : "";
        const auto r = show(f, no_project_state);
        const auto *e = find_button(r, text, id);
        if (e == nullptr) {
            std::fprintf(stderr, "  %s: no button '%s'\n", name_of(kind), text.c_str());
            RV_EDITOR_CHECK(e != nullptr);
            continue;
        }
        const rv_editor_probe_entry button = *e;
        const std::string profile_name = f.app().run_config.profiles.empty() ? "" : f.app().run_config.profiles[0].name;
        const size_t profiles = f.app().run_config.profiles.size();
        const std::set<std::string> where_before = listing(f.where, true);
        hover(f, button);
        const bool reason = tooltip_is_a_reason(gui);
        if (!reason) {
            std::fprintf(stderr,
                "  %s '%s%s': no tooltip with the reason for the missing project\n",
                name_of(kind),
                text.c_str(),
                id != nullptr && text.empty() ? id : "");
        }
        RV_EDITOR_CHECK(reason);
        if (!reason) {
            // Live on this implementation: pressing could write files or fail, so it is not pressed.
            continue;
        }
        ImGuiIO &io = ImGui::GetIO();
        io.DeltaTime = 1.0f / 60.0f;
        io.AddMouseButtonEvent(0, true);
        f.draw(roomy_w, roomy_h);
        const bool took_it = ImGui::GetCurrentContext()->ActiveId == button.id;
        RV_EDITOR_CHECK(!took_it);
        io.AddMousePosEvent(-1000.0f, -1000.0f);
        f.draw(roomy_w, roomy_h);
        io.AddMouseButtonEvent(0, false);
        f.draw(roomy_w, roomy_h);
        for (const auto kind_asked : request_kinds) {
            RV_EDITOR_CHECK(!rv_editor_app_request_pending(f.app(), kind_asked));
        }
        RV_EDITOR_CHECK(f.app().run_config.profiles.size() == profiles);
        RV_EDITOR_CHECK(f.app().run_config.profiles.empty() || f.app().run_config.profiles[0].name == profile_name);
        RV_EDITOR_CHECK(listing(f.where, true) == where_before);
    }
    RV_EDITOR_CHECK(listing(fs::current_path(), false) == cwd_before);
}

// A character typed into the field `id` of the no-project state changes nothing.
void check_read_only(fixture &f, const char *id, char *value)
{
    const auto r = show(f, no_project_state);
    const auto *field = find_id(r, id);
    if (field == nullptr) {
        std::fprintf(stderr, "  no field '%s'\n", id);
        RV_EDITOR_CHECK(field != nullptr);
        return;
    }
    const std::string before = value;
    f.click(*field, roomy_w, roomy_h);
    ImGui::GetIO().AddInputCharacter('x');
    f.draw(roomy_w, roomy_h);
    f.draw(roomy_w, roomy_h);
    if (before != value) {
        std::fprintf(stderr, "  '%s' took the typed character: '%s'\n", id, value);
    }
    RV_EDITOR_CHECK(before == value);
}

// The frames and the button row with no project sit where they do with one.
void check_places(rv_editor_pane_kind kind, const std::vector<std::string> &anchors)
{
    load_texts();
    rv_editor_test_imgui gui(true);
    fixture f(gui, kind);
    for (const ImVec2 size : tile_sizes(gui, kind, ImVec2(roomy_w, roomy_h))) {
        const auto none = show(f, no_project_state, size.x, size.y);
        const auto open = show(f, with_project_state, size.x, size.y);
        if (kind == rv_editor_pane_kind::run_config) {
            // R2: "No project is open" is a strip under the area, which gives up its bottom: the top, the shelf and what
            // stands under the area are the same with a project.
            const std::string top = strip::same_top(none, open);
            const std::string moved = same_items(strip::outside_area(none), strip::outside_area(open), anchors);
            if (!top.empty() || !moved.empty()) {
                std::fprintf(stderr, "  %s: %s %s\n", name_of(kind), top.c_str(), moved.c_str());
            }
            RV_EDITOR_CHECK(top.empty() && moved.empty());
            continue;
        }
        const std::string frames = windows_difference(none, open);
        if (!frames.empty()) {
            std::fprintf(stderr, "  %s: %s\n", name_of(kind), frames.c_str());
        }
        RV_EDITOR_CHECK(frames.empty());
        const std::string moved = same_items(frames_only(none), frames_only(open), anchors);
        if (!moved.empty()) {
            std::fprintf(stderr, "  %s: %s\n", name_of(kind), moved.c_str());
        }
        RV_EDITOR_CHECK(moved.empty());
    }
}

} // namespace

// Run Config: Apply, Apply and Restart, Revert, New Profile and Delete are disabled with the reason, a press does nothing.
RV_EDITOR_TEST(rv_editor_test_s15_run_config_without_a_project_has_no_live_buttons)
{
    check_buttons(rv_editor_pane_kind::run_config,
        { { "pane_run_config.apply_label", nullptr },
            { "pane_run_config.apply_restart_label", nullptr },
            { "pane_run_config.revert_label", nullptr },
            { nullptr, "##new" },
            { nullptr, "##delete" } });
}

RV_EDITOR_TEST(rv_editor_test_s15_run_config_without_a_project_keeps_its_places)
{
    load_texts();
    check_places(rv_editor_pane_kind::run_config,
        { rv_editor_text("pane_run_config.apply_label"),
            rv_editor_text("pane_run_config.apply_restart_label"),
            rv_editor_text("pane_run_config.revert_label") });
}

// Findings: Capture Frame and Record Finding are disabled with the reason, a press does nothing, the fields are read only.
RV_EDITOR_TEST(rv_editor_test_s15_findings_without_a_project_has_no_live_buttons)
{
    load_texts();
    check_buttons(rv_editor_pane_kind::findings,
        { { "findings.capture_frame", nullptr }, { "findings.record_finding", nullptr } });
}

RV_EDITOR_TEST(rv_editor_test_s15_findings_without_a_project_has_read_only_fields)
{
    load_texts();
    rv_editor_test_imgui gui(true);
    fixture f(gui, rv_editor_pane_kind::findings);
    auto &fi = f.app().findings;
    check_read_only(f, (std::string("##") + rv_editor_text("findings.title")).c_str(), fi.title);
    check_read_only(f, (std::string("##") + rv_editor_text("findings.steps")).c_str(), fi.steps);
    check_read_only(f, (std::string("##") + rv_editor_text("findings.expected")).c_str(), fi.expected);
    check_read_only(f, (std::string("##") + rv_editor_text("findings.actual")).c_str(), fi.actual);
    // Without the title typed, Record Finding stays disabled with the reason for the missing project all the same.
    RV_EDITOR_CHECK(fi.title[0] == '\0');
}

RV_EDITOR_TEST(rv_editor_test_s15_findings_without_a_project_keeps_its_places)
{
    load_texts();
    check_places(rv_editor_pane_kind::findings,
        { rv_editor_text("findings.capture_frame"), rv_editor_text("findings.record_finding") });
}

// Test Case: Passed, Failed, Blocked and New Test Case are disabled with the reason, a press does nothing.
RV_EDITOR_TEST(rv_editor_test_s15_test_case_without_a_project_has_no_live_buttons)
{
    load_texts();
    check_buttons(rv_editor_pane_kind::test_case,
        { { "findings.passed", nullptr },
            { "findings.failed", nullptr },
            { "findings.blocked", nullptr },
            { "findings.new_test_case", nullptr } });
}

RV_EDITOR_TEST(rv_editor_test_s15_test_case_without_a_project_has_a_read_only_note)
{
    load_texts();
    rv_editor_test_imgui gui(true);
    fixture f(gui, rv_editor_pane_kind::test_case);
    check_read_only(f, "##note", f.app().findings.note);
}

RV_EDITOR_TEST(rv_editor_test_s15_test_case_without_a_project_keeps_its_places)
{
    load_texts();
    check_places(rv_editor_pane_kind::test_case,
        { rv_editor_text("findings.passed"),
            rv_editor_text("findings.failed"),
            rv_editor_text("findings.blocked"),
            rv_editor_text("findings.new_test_case") });
}
