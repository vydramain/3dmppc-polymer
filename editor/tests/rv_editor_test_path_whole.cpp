// Paths shown whole: a path field and a path row never add "..."; inside the project a path is relative to the root,
// outside it in full; a path row has no standing Copy button, Copy is in its context menu.

#include <string>

#include "imgui_internal.h"
#include "rv_editor_test.hpp"
#include "rv_editor_test_imgui.hpp"
#include "rv_editor_test_layout_probe.hpp"
#include "text/rv_editor_text.hpp"
#include "theme/rv_editor_theme_imgui.hpp"
#include "ui/rv_editor_widgets.hpp"

using namespace rv_editor;
using namespace rv_editor_test;

namespace
{

constexpr rv_editor_theme theme = rv_editor_theme_olive;
constexpr float field_width = 200.0f; // narrower than every path below
const std::string root = "/home/user/project";

void load_texts()
{
    std::string error;
    RV_EDITOR_CHECK(rv_editor_text_load({}, error) == 0);
}

std::string deep(const std::string &prefix)
{
    std::string path = prefix;
    while (path.size() < 110) {
        path += "/deeply_nested_dir";
    }
    return path + "/engine.cfg";
}

rv_editor_path_probe field_shown(const std::string &path)
{
    rv_editor_test_imgui gui;
    load_texts();
    rv_editor_path_probe probe;
    std::string buf = path;
    buf.resize(512);
    for (int i = 0; i < 2; ++i) {
        gui.frame([&] {
            ImGui::SetNextItemWidth(field_width);
            rv_editor_path_field("##path", buf.data(), buf.size(), root, theme, {}, &probe);
        });
    }
    return probe;
}

// The entry of a path row's path text: an unlabelled item as wide as the whole path.
const rv_editor_probe_entry *path_text(const rv_editor_probe_record &r, float whole)
{
    for (const auto &e : r.entries) {
        if (e.label.empty() && e.x1 - e.x0 >= whole - 1.0f && e.x1 - e.x0 <= whole + 1.0f) {
            return &e;
        }
    }
    return nullptr;
}

} // namespace

RV_EDITOR_TEST(rv_editor_test_path_field_inside_the_project_shows_the_path_from_its_root)
{
    const std::string rel = deep("src");
    const rv_editor_path_probe probe = field_shown(root + "/" + rel);
    RV_EDITOR_CHECK(probe.shown == rel);
    RV_EDITOR_CHECK(probe.shown.rfind("src/", 0) == 0);
    RV_EDITOR_CHECK(probe.shown.find("...") == std::string::npos);
    RV_EDITOR_CHECK(probe.tooltip == root + "/" + rel);
}

RV_EDITOR_TEST(rv_editor_test_path_field_outside_the_project_shows_the_whole_path)
{
    const std::string path = deep("/var/tmp/outside");
    const rv_editor_path_probe probe = field_shown(path);
    RV_EDITOR_CHECK(probe.shown == path);
    RV_EDITOR_CHECK(probe.shown.find("...") == std::string::npos);
    RV_EDITOR_CHECK(probe.tooltip == path);
}

RV_EDITOR_TEST(rv_editor_test_path_row_shows_the_whole_path_in_a_wide_and_in_a_narrow_row)
{
    load_texts();
    rv_editor_test_imgui gui;
    const std::string path = deep("/home/user/projects/with/a/long/root");
    float whole = 0;
    gui.frame([&] {
        whole = ImGui::CalcTextSize(path.c_str()).x;
    });
    for (const float width : { whole + 400.0f, 300.0f }) {
        const auto body = [&](rv_editor_probe_record &) {
            rv_editor_path_row("Root", path, theme);
        };
        rv_editor_probe_draw(gui, width, body);
        const auto r = rv_editor_probe_draw(gui, width, body);
        // The path is one text as wide as itself: never shortened with "..." (a narrow row scrolls or overflows).
        RV_EDITOR_CHECK(path_text(r, whole) != nullptr);
    }
}

RV_EDITOR_TEST(rv_editor_test_path_row_has_no_standing_copy_button_and_copies_from_its_context_menu)
{
    load_texts();
    rv_editor_test_imgui gui;
    const std::string path = "/home/user/project/export/web/index.html";
    const std::string copy = rv_editor_text("widgets_fields.button_copy");
    float whole = 0;
    gui.frame([&] {
        whole = ImGui::CalcTextSize(path.c_str()).x;
    });
    const auto body = [&](rv_editor_probe_record &) {
        rv_editor_path_row("Root", path, theme);
    };
    const auto draw = [&] {
        return rv_editor_probe_draw(gui, 900.0f, body);
    };
    draw();
    auto r = draw();
    RV_EDITOR_CHECK(!r.entries.empty());
    const auto standing = [&](const rv_editor_probe_record &rec) {
        for (const auto &e : rec.entries) {
            if (e.label.rfind(copy, 0) == 0) {
                return true;
            }
        }
        return false;
    };
    RV_EDITOR_CHECK(!standing(r));
    const auto *text = path_text(r, whole);
    RV_EDITOR_CHECK(text != nullptr);
    if (text == nullptr) {
        return;
    }
    ImGuiIO &io = ImGui::GetIO();
    io.AddMousePosEvent((text->x0 + text->x1) / 2.0f, (text->y0 + text->y1) / 2.0f);
    draw();
    io.AddMouseButtonEvent(1, true);
    draw();
    io.AddMouseButtonEvent(1, false);
    draw();
    r = draw();
    RV_EDITOR_CHECK(standing(r)); // the context menu offers Copy
    const rv_editor_probe_entry *item = nullptr;
    for (const auto &e : r.entries) {
        if (e.label.rfind(copy, 0) == 0) {
            item = &e;
        }
    }
    if (item == nullptr) {
        return;
    }
    ImGui::SetClipboardText("");
    io.AddMousePosEvent((item->x0 + item->x1) / 2.0f, (item->y0 + item->y1) / 2.0f);
    draw();
    io.AddMouseButtonEvent(0, true);
    draw();
    io.AddMouseButtonEvent(0, false);
    draw();
    draw();
    RV_EDITOR_CHECK(std::string(ImGui::GetClipboardText()) == path);
}
