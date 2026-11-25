// AC5, AC9 and AC13 in the Scene, Hierarchy and Inspector panes: a long label never covers its field, a value too long
// for its buffer keeps its field, a Reset and a Clear Picks are always there, and within a mode nothing appears, moves
// or resizes by state (a note, an ask, an error line, a banner): the viewport and the tree keep their rectangles.

#include <algorithm>
#include <cstring>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "app/rv_editor_shell.hpp"
#include "imgui_internal.h"
#include "panes/rv_editor_panes.hpp"
#include "pdkkit/component/rv_component_record.hpp"
#include "project/rv_editor_descriptions.hpp"
#include "rv_editor_test.hpp"
#include "rv_editor_test_pane_state.hpp"
#include "rv_editor_test_strip.hpp"
#include "scene/rv_editor_scene_edit.hpp"
#include "theme/rv_editor_theme_imgui.hpp"

using namespace rv_editor;
using namespace rv_editor_test;
using namespace rv_editor_test::pane_state;

namespace
{

constexpr float width = 400.0f;
const rv_editor_theme theme = rv_editor_theme_olive;
const char *const long_label = "a_field_with_a_very_long_name_"; // 30 characters
const char *const comp_name = "probe_comp";

// An open project with a pdkkit scene of two objects, the second selected, and a described component on it.
struct scene_fixture {
    tall_gui g;
    rv_editor_app app;
    rv_editor_ui ui;
    std::vector<rv_editor_ui_request> requests;
    rv_editor_ui_port port{ ui, requests };
    fs::path where = fresh_dir("rv_editor_stable_scene_panes");
    scene_fixture()
    {
        open_project(app, where);
        app.project.sdk = rv_editor_sdk_state::pdkkit;
        app.scene = std::make_unique<rv_editor_scene_doc>();
        app.scene->scene = rv_editor_scene_make(where / "scenes" / "a.scene.toml");
        app.scene->selected = app.scene->scene.objects[1].id;

        rv_pdkkit::ecs_component_desc desc{};
        std::strcpy(desc.name, comp_name);
        desc.field_count = 1;
        std::strcpy(desc.fields[0].name, long_label);
        desc.fields[0].type = rv_pdkkit::ecs_field_type::string;
        std::strcpy(desc.fields[0].def.text, "dflt");
        app.descriptions.snapshot.state = rv_editor_descriptions_state::loaded;
        app.descriptions.snapshot.records.push_back(rv_pdkkit::ecs_record_make(desc));
        // A second description keeps the Add component button below the fields.
        rv_pdkkit::ecs_component_desc other{};
        std::strcpy(other.name, "other_comp");
        app.descriptions.snapshot.records.push_back(rv_pdkkit::ecs_record_make(other));
        set_value(nullptr);
    }
    ~scene_fixture()
    {
        remove_dir(where);
    }
    rv_editor_scene_object &object()
    {
        return app.scene->scene.objects[1];
    }
    // The probe component holds `value` (nullptr: the field is at its default) for the long-named field.
    void set_value(const char *value)
    {
        auto &components = object().components;
        components.erase(std::remove_if(components.begin(),
                             components.end(),
                             [](const rv_editor_scene_component &c) {
                                 return c.name == comp_name;
                             }),
            components.end());
        rv_editor_scene_component c;
        c.name = comp_name;
        if (value != nullptr) {
            rv_pdklib::rv_manifest_tree_entry e;
            e.key = long_label;
            e.value.str = value;
            c.fields.push_back(e);
        }
        components.push_back(c);
    }
    rv_editor_probe_record draw(const std::function<void()> &body, float w = width)
    {
        return settled(g, w, body);
    }
    rv_editor_probe_record inspector(float w = width)
    {
        return draw(
            [this] {
                rv_editor_pane_scene_inspector(app, theme, port);
            },
            w);
    }
    rv_editor_probe_record hierarchy(float w = width)
    {
        return draw(
            [this] {
                rv_editor_pane_hierarchy(app, theme, port);
            },
            w);
    }
    rv_editor_probe_record scene(float w = width)
    {
        return draw(
            [this] {
                rv_editor_pane_scene(app, nullptr, theme, port);
            },
            w);
    }
};

void report(const std::string &d)
{
    if (!d.empty()) {
        std::fprintf(stderr, "  %s\n", d.c_str());
    }
}

int count_items(const rv_editor_probe_record &r, const char *label)
{
    int n = 0;
    while (nth_item(r, label, n) != nullptr) {
        ++n;
    }
    return n;
}

// "" when no item with an id of `a` moved or resized in `b`; items only `b` has are not looked at.
std::string moved_items(const rv_editor_probe_record &a, const rv_editor_probe_record &b)
{
    for (const auto &ea : a.entries) {
        if (ea.id == 0) {
            continue;
        }
        const rv_editor_probe_entry *eb = nullptr;
        for (const auto &e : b.entries) {
            if (e.id == ea.id) {
                eb = &e;
                break;
            }
        }
        if (eb == nullptr) {
            return rv_editor_probe_detail::name(ea) + " is missing from the second draw";
        }
        if (ea.x0 != eb->x0 || ea.y0 != eb->y0 || ea.x1 != eb->x1 || ea.y1 != eb->y1) {
            return rv_editor_probe_detail::name(ea) + " " + rv_editor_probe_detail::rect(ea) + " became " +
                rv_editor_probe_detail::rect(*eb);
        }
    }
    return "";
}

// "" when the item `label` starts clear of the field's own name drawn at the pane's left edge (the Visible check box
// marks it), or on a line of its own below the name; else why not. The name is drawn without an item, so its width is
// measured.
std::string label_over(scene_fixture &f, const char *label, const char *name)
{
    const auto r = f.inspector();
    const auto *field = find_item(r, label);
    const auto *left = find_item(r, "Visible");
    const auto *section = find_item(r, "Remove");
    if (field == nullptr || left == nullptr || section == nullptr) {
        return std::string("'") + label + "' is not drawn";
    }
    float name_width = 0.0f;
    float line = 0.0f;
    f.g.gui.frame([&] {
        name_width = ImGui::CalcTextSize(name).x;
        line = ImGui::GetTextLineHeight();
    });
    const bool clear_of_name = field->x0 >= left->x0 + name_width;
    const bool below_name = field->y0 >= section->y1 + line;
    if (clear_of_name || below_name) {
        return "";
    }
    return "'" + std::string(label) + "' " + rv_editor_probe_detail::rect(*field) + " starts inside the name of " +
        std::to_string(name_width) + " px at " + std::to_string(left->x0);
}

const char *const viewport = "##viewport";

// Draws with the viewport present, so a missing viewport is not mistaken for a failure of the contract.
bool has_viewport(const rv_editor_probe_record &r)
{
    return find_item(r, viewport) != nullptr;
}

// Closes the New Scene ask by Escape, so the next test starts with it closed (its state is the shell's, per process).
void close_new_scene_ask(scene_fixture &f)
{
    ImGuiIO &io = ImGui::GetIO();
    io.AddKeyEvent(ImGuiKey_Escape, true);
    f.scene(width);
    io.AddKeyEvent(ImGuiKey_Escape, false);
    f.scene(width);
}

} // namespace

// ---- Inspector -------------------------------------------------------------------------------------------------------

RV_EDITOR_TEST(rv_editor_test_stable_scene_inspector_long_field_label_does_not_cover_its_field)
{
    scene_fixture f;
    const std::string d = label_over(f, "##value", long_label);
    report(d);
    RV_EDITOR_CHECK(d.empty());
    f.set_value("changed");
    const std::string d2 = label_over(f, "##value", long_label);
    report(d2);
    RV_EDITOR_CHECK(d2.empty());
}

RV_EDITOR_TEST(rv_editor_test_stable_scene_inspector_reset_is_there_at_default_and_changed)
{
    scene_fixture f;
    RV_EDITOR_CHECK(count_items(f.inspector(), "Reset") == 1); // default: disabled, with its reason
    f.set_value("changed");
    RV_EDITOR_CHECK(count_items(f.inspector(), "Reset") == 1);
}

RV_EDITOR_TEST(rv_editor_test_stable_scene_inspector_default_and_changed_value_keep_the_layout)
{
    scene_fixture f;
    const auto base = f.inspector();
    f.set_value("changed");
    const std::string d = layout_difference(base, f.inspector());
    report(d);
    RV_EDITOR_CHECK(d.empty());
}

RV_EDITOR_TEST(rv_editor_test_stable_scene_inspector_too_long_value_keeps_a_field_of_the_same_rect)
{
    scene_fixture f;
    f.set_value("changed");
    const auto base = f.inspector();
    f.set_value(std::string(300, 'x').c_str()); // longer than the field's buffer
    const auto longer = f.inspector();
    const std::string d = item_difference(base, longer, "##value");
    report(d);
    RV_EDITOR_CHECK(d.empty());
}

RV_EDITOR_TEST(rv_editor_test_stable_scene_inspector_refusal_row_is_reserved)
{
    scene_fixture f;
    f.set_value("changed");
    const auto base = f.inspector();
    rv_editor_scene_doc &doc = *f.app.scene;
    doc.field_refusal = { f.object().id,
        comp_name,
        long_label,
        "the value does not read as a string",
        doc.steps,
        doc.undo.size() };
    const std::string d = layout_difference(strip::bottom_free(base), strip::bottom_free(f.inspector()));
    report(d);
    RV_EDITOR_CHECK(d.empty());
}

RV_EDITOR_TEST(rv_editor_test_stable_scene_inspector_read_only_note_keeps_its_place)
{
    scene_fixture f;
    const auto base = f.inspector();
    f.app.scene->scene.read_only = "the file is of an older form";
    const std::string d = layout_difference(strip::bottom_free(base), strip::bottom_free(f.inspector()));
    report(d);
    RV_EDITOR_CHECK(d.empty());
}

// ---- Hierarchy -------------------------------------------------------------------------------------------------------

RV_EDITOR_TEST(rv_editor_test_stable_scene_hierarchy_move_note_does_not_move_the_tree)
{
    scene_fixture f;
    const auto base = f.hierarchy();
    f.app.scene_move_note = "Not moved: an object cannot go under its own child, so the move is refused";
    const auto noted = f.hierarchy();
    const std::string d = moved_items(strip::bottom_free(base), strip::bottom_free(noted));
    report(d);
    RV_EDITOR_CHECK(d.empty());
    RV_EDITOR_CHECK(base.content_height == noted.content_height);
}

RV_EDITOR_TEST(rv_editor_test_stable_scene_hierarchy_clear_picks_is_there_with_and_without_picks)
{
    scene_fixture f;
    const int without = count_items(f.hierarchy(), "##command"); // Add is a letter button too
    rv_editor_scene_doc &doc = *f.app.scene;
    doc.picked_owner = doc.selected;
    doc.picked_count = 4;
    doc.picked_pieces = { 1 };
    const int with = count_items(f.hierarchy(), "##command");
    RV_EDITOR_CHECK(with > 0);
    RV_EDITOR_CHECK(without == with); // Clear Picks is there, disabled, when nothing is picked
}

RV_EDITOR_TEST(rv_editor_test_stable_scene_hierarchy_picks_keep_the_layout)
{
    scene_fixture f;
    const auto base = f.hierarchy();
    rv_editor_scene_doc &doc = *f.app.scene;
    doc.picked_owner = doc.selected;
    doc.picked_count = 4;
    doc.picked_pieces = { 1 };
    const std::string d = layout_difference(base, f.hierarchy());
    report(d);
    RV_EDITOR_CHECK(d.empty());
}

// ---- Scene -----------------------------------------------------------------------------------------------------------

RV_EDITOR_TEST(rv_editor_test_stable_scene_new_scene_ask_does_not_move_the_viewport)
{
    scene_fixture f;
    const auto closed = f.scene();
    RV_EDITOR_CHECK(has_viewport(closed));
    rv_editor_shell_new_scene_request(f.app);
    const auto open = f.scene();
    close_new_scene_ask(f);
    const std::string d = item_difference(closed, open, viewport, false);
    report(d);
    RV_EDITOR_CHECK(d.empty());
}

RV_EDITOR_TEST(rv_editor_test_stable_scene_error_line_does_not_move_the_viewport)
{
    scene_fixture f;
    const auto base = f.scene();
    RV_EDITOR_CHECK(has_viewport(base));
    f.app.scene_error = "scenes/b.scene.toml: could not be opened because the file is not a scene of the form 2";
    const std::string d = item_difference(base, f.scene(), viewport);
    report(d);
    RV_EDITOR_CHECK(d.empty());
}

RV_EDITOR_TEST(rv_editor_test_stable_scene_read_only_line_does_not_move_the_viewport)
{
    scene_fixture f;
    const auto base = f.scene();
    RV_EDITOR_CHECK(has_viewport(base));
    f.app.scene->scene.read_only = "the file is of an older form and is not written back";
    const std::string d = item_difference(base, f.scene(), viewport);
    report(d);
    RV_EDITOR_CHECK(d.empty());
}
