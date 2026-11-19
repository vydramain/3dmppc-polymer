// A path that does not fit shows its END with a leading "...": the tail function, the relative display, the path
// field (tail unfocused, whole when focused, tooltip with the full path), and the pane header.

#include <string>

#include "imgui_internal.h"
#include "rv_editor_test.hpp"
#include "rv_editor_test_imgui.hpp"
#include "text/rv_editor_text.hpp"
#include "theme/rv_editor_theme_imgui.hpp"
#include "ui/rv_editor_widgets.hpp"

using namespace rv_editor;

namespace
{

constexpr float narrow = 200.0f;

void load_texts()
{
    std::string error;
    RV_EDITOR_CHECK(rv_editor_text_load({}, error) == 0);
}

// 120 characters ending in a file name.
std::string long_path()
{
    std::string path = "/home/user/projects/";
    while (path.size() < 100) {
        path += "deeply_nested_dir/";
    }
    path.resize(100);
    path += "/engine.cfg";
    path.insert(0, 120 - path.size(), 'x');
    return path;
}

bool ends_with(const std::string &s, const std::string &tail)
{
    return s.size() >= tail.size() && s.compare(s.size() - tail.size(), tail.size(), tail) == 0;
}

// The path field in a 200 px item for one frame.
std::string field_frame(rv_editor_test::rv_editor_test_imgui &gui,
    std::string &value,
    const std::string &root,
    rv_editor_path_probe &probe,
    bool focus,
    bool drop_focus = false)
{
    const rv_editor_theme theme = rv_editor_theme_olive;
    std::string buf = value;
    buf.resize(512);
    gui.frame([&] {
        if (drop_focus) {
            ImGui::ClearActiveID();
        }
        if (focus) {
            ImGui::SetKeyboardFocusHere();
        }
        ImGui::SetNextItemWidth(narrow);
        rv_editor_path_field("##path", buf.data(), buf.size(), root, theme, {}, &probe);
    });
    return buf.c_str();
}

} // namespace

RV_EDITOR_TEST(rv_editor_test_path_tail_keeps_the_end_with_ellipsis)
{
    load_texts();
    rv_editor_test::rv_editor_test_imgui gui;
    const std::string path = long_path();
    std::string shown;
    float width = 0.0f;
    gui.frame([&] {
        shown = rv_editor_path_tail(path, narrow);
        width = ImGui::CalcTextSize(shown.c_str()).x;
    });
    RV_EDITOR_CHECK(shown.rfind("...", 0) == 0);
    RV_EDITOR_CHECK(ends_with(shown, "engine.cfg"));
    RV_EDITOR_CHECK(ends_with(path, shown.substr(3)));
    RV_EDITOR_CHECK(width <= narrow);
    RV_EDITOR_CHECK(shown.size() < path.size());
}

RV_EDITOR_TEST(rv_editor_test_path_tail_fills_the_width)
{
    load_texts();
    rv_editor_test::rv_editor_test_imgui gui;
    const std::string path = long_path();
    float width = 0.0f;
    float one_more = 0.0f;
    gui.frame([&] {
        const std::string shown = rv_editor_path_tail(path, narrow);
        width = ImGui::CalcTextSize(shown.c_str()).x;
        // Not cut shorter than needed: one more character of the path would not fit.
        const std::string more = "..." + path.substr(path.size() - (shown.size() - 3) - 1);
        one_more = ImGui::CalcTextSize(more.c_str()).x;
    });
    RV_EDITOR_CHECK(width <= narrow);
    RV_EDITOR_CHECK(one_more > narrow);
}

RV_EDITOR_TEST(rv_editor_test_path_tail_short_path_unchanged)
{
    load_texts();
    rv_editor_test::rv_editor_test_imgui gui;
    std::string shown;
    gui.frame([&] {
        shown = rv_editor_path_tail("export/web", narrow);
    });
    RV_EDITOR_CHECK(shown == "export/web");
}

RV_EDITOR_TEST(rv_editor_test_path_tail_keeps_utf8_whole)
{
    load_texts();
    rv_editor_test::rv_editor_test_imgui gui;
    std::string path;
    for (int i = 0; i < 60; ++i) {
        path += "\xD0\xBF\xD0\xB0"; // two Cyrillic letters
    }
    std::string shown;
    gui.frame([&] {
        shown = rv_editor_path_tail(path, narrow);
    });
    RV_EDITOR_CHECK(shown.size() < path.size());
    RV_EDITOR_CHECK(shown.size() > 3 && (static_cast<unsigned char>(shown[3]) & 0xC0) != 0x80);
}

RV_EDITOR_TEST(rv_editor_test_path_relative_inside_root)
{
    RV_EDITOR_CHECK(rv_editor_path_relative("/home/u/proj/export/web", "/home/u/proj") == "export/web");
    RV_EDITOR_CHECK(rv_editor_path_relative("/home/u/proj/export/web", "/home/u/proj/") == "export/web");
}

RV_EDITOR_TEST(rv_editor_test_path_relative_outside_root_stays_full)
{
    RV_EDITOR_CHECK(rv_editor_path_relative("/var/tmp/out", "/home/u/proj") == "/var/tmp/out");
    // A sibling that only shares the prefix is not inside.
    RV_EDITOR_CHECK(rv_editor_path_relative("/home/u/proj2/x", "/home/u/proj") == "/home/u/proj2/x");
    RV_EDITOR_CHECK(rv_editor_path_relative("/home/u/proj/x", "") == "/home/u/proj/x");
}

RV_EDITOR_TEST(rv_editor_test_path_field_unfocused_shows_tail_and_full_tooltip)
{
    load_texts();
    rv_editor_test::rv_editor_test_imgui gui;
    const std::string path = long_path();
    std::string value = path;
    rv_editor_path_probe probe;
    field_frame(gui, value, "", probe, false);
    field_frame(gui, value, "", probe, false);
    RV_EDITOR_CHECK(probe.shown.rfind("...", 0) == 0);
    RV_EDITOR_CHECK(ends_with(probe.shown, "engine.cfg"));
    RV_EDITOR_CHECK(!probe.editing);
    RV_EDITOR_CHECK(probe.tooltip == path);
}

RV_EDITOR_TEST(rv_editor_test_path_field_unfocused_inside_root_is_relative)
{
    load_texts();
    rv_editor_test::rv_editor_test_imgui gui;
    const std::string path = "/home/u/proj/export/web";
    std::string value = path;
    rv_editor_path_probe probe;
    field_frame(gui, value, "/home/u/proj", probe, false);
    field_frame(gui, value, "/home/u/proj", probe, false);
    RV_EDITOR_CHECK(probe.shown == "export/web");
    RV_EDITOR_CHECK(probe.tooltip == path);
}

RV_EDITOR_TEST(rv_editor_test_path_field_focused_edits_the_whole_value)
{
    load_texts();
    rv_editor_test::rv_editor_test_imgui gui;
    const std::string path = long_path();
    std::string value = path;
    rv_editor_path_probe probe;
    field_frame(gui, value, "", probe, true);
    field_frame(gui, value, "", probe, false);
    RV_EDITOR_CHECK(probe.editing);
    RV_EDITOR_CHECK(probe.shown == path);
    RV_EDITOR_CHECK(value == path);
}

RV_EDITOR_TEST(rv_editor_test_path_field_after_leaving_shows_the_tail_again)
{
    load_texts();
    rv_editor_test::rv_editor_test_imgui gui;
    const std::string path = long_path();
    std::string value = path;
    rv_editor_path_probe probe;
    field_frame(gui, value, "", probe, true);
    field_frame(gui, value, "", probe, false);
    field_frame(gui, value, "", probe, false, true);
    field_frame(gui, value, "", probe, false);
    RV_EDITOR_CHECK(!probe.editing);
    RV_EDITOR_CHECK(probe.shown.rfind("...", 0) == 0);
    RV_EDITOR_CHECK(ends_with(probe.shown, "engine.cfg"));
    RV_EDITOR_CHECK(value == path);
}

RV_EDITOR_TEST(rv_editor_test_pane_header_path_title_keeps_the_tail)
{
    load_texts();
    rv_editor_test::rv_editor_test_imgui gui;
    const std::string path = long_path();
    std::string shown;
    std::string plain;
    gui.frame([&] {
        shown = rv_editor_pane_header_title(path.c_str(), narrow);
        plain = rv_editor_pane_header_title("Short", narrow);
    });
    RV_EDITOR_CHECK(shown.rfind("...", 0) == 0);
    RV_EDITOR_CHECK(ends_with(shown, "engine.cfg"));
    RV_EDITOR_CHECK(plain == "Short");
}
