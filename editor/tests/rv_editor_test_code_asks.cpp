// The code tile and the shell's unsaved-file asks keep their places: a path in the status line, a file tab or the title
// keeps its end, Save As adapts to the tile, the asks and the swap shelf take a constant place, buttons wrap inside the
// tile, and Review Changes keeps its blocks in place between idle, saving and failures.
// nvim is not started here: the pure pieces of the code tile (text fitting, the swap shelf) are tested directly.

#include <cstdio>
#include <string>
#include <vector>

#include "app/rv_editor_shell.hpp"
#include "imgui_internal.h"
#include "panes/rv_editor_panes.hpp"
#include "rv_editor_test.hpp"
#include "rv_editor_test_imgui.hpp"
#include "rv_editor_test_layout_probe.hpp"
#include "text/rv_editor_text.hpp"
#include "ui/rv_editor_widgets.hpp"

using namespace rv_editor;
using namespace rv_editor_test;

namespace
{

constexpr rv_editor_theme theme = rv_editor_theme_olive;
constexpr float narrow = 200.0f;
const std::string root = "/home/user/project";

void load_texts()
{
    std::string error;
    RV_EDITOR_CHECK(rv_editor_text_load(std::filesystem::path{}, error) == 0);
}

// A path inside the project root, 120+ characters, ending in a file name.
std::string long_path()
{
    std::string path = root + "/";
    while (path.size() < 110) {
        path += "deeply_nested_dir/";
    }
    return path + "engine.cfg";
}

bool ends_with(const std::string &s, const std::string &tail)
{
    return s.size() >= tail.size() && s.compare(s.size() - tail.size(), tail.size(), tail) == 0;
}

const rv_editor_probe_entry *find(const rv_editor_probe_record &r, const std::string &label)
{
    for (const auto &e : r.entries) {
        if (e.label == label || e.label.rfind(label + "##", 0) == 0) {
            return &e;
        }
    }
    return nullptr;
}

// "" when every recorded item lies inside the window width.
std::string outside(const rv_editor_probe_record &r, float width)
{
    for (const auto &e : r.entries) {
        // ImGui's own "Debug" window of the first frame (a rectangle at 60,60) is not the tile's.
        if (e.x0 >= 60.0f && e.y0 >= 60.0f && e.y0 <= 63.0f) {
            continue;
        }
        if (e.x1 > width + 0.5f) {
            return rv_editor_probe_detail::name(e) + " " + rv_editor_probe_detail::rect(e) + " is past " +
                std::to_string(width);
        }
    }
    return "";
}

// The stand-in for the code grid: an item under what the tile asks above it.
void grid(rv_editor_probe_record &p)
{
    ImGui::Button("grid");
    p.item("grid");
}

// "" when the "grid" items are at the same rect in both draws.
std::string grid_difference(const rv_editor_probe_record &a, const rv_editor_probe_record &b)
{
    const auto *x = find(a, "grid");
    const auto *y = find(b, "grid");
    if (x == nullptr || y == nullptr) {
        return "no grid item";
    }
    if (x->x0 != y->x0 || x->y0 != y->y0 || x->x1 != y->x1 || x->y1 != y->y1) {
        return "grid " + rv_editor_probe_detail::rect(*x) + " became " + rv_editor_probe_detail::rect(*y);
    }
    return "";
}

// True when the labelled item exists in both draws at the same rect.
bool same_rect(const rv_editor_probe_record &a, const rv_editor_probe_record &b, const std::string &label)
{
    const auto *x = find(a, label);
    const auto *y = find(b, label);
    return x != nullptr && y != nullptr && x->x0 == y->x0 && x->y0 == y->y0 && x->x1 == y->x1 && x->y1 == y->y1;
}

} // namespace

// AC7: Code status line

RV_EDITOR_TEST(rv_editor_test_code_status_line_short_path_is_relative_with_tooltip)
{
    load_texts();
    rv_editor_test_imgui gui;
    const std::string path = root + "/scripts/main.lua";
    rv_editor_code_text t;
    gui.frame([&] {
        t = rv_editor_code_status_text(path, root, false, 400.0f);
    });
    RV_EDITOR_CHECK(t.shown == "scripts/main.lua");
    RV_EDITOR_CHECK(t.tooltip == path);
}

RV_EDITOR_TEST(rv_editor_test_code_status_line_untitled_has_no_path_tooltip)
{
    load_texts();
    rv_editor_test_imgui gui;
    rv_editor_code_text t;
    gui.frame([&] {
        t = rv_editor_code_status_text("", root, false, narrow);
    });
    RV_EDITOR_CHECK(t.shown == rv_editor_text("pane_code.untitled_document"));
    RV_EDITOR_CHECK(t.tooltip.empty());
}

// AC7: a code tab label

RV_EDITOR_TEST(rv_editor_test_code_tab_label_short_file_still_has_the_full_path_tooltip)
{
    load_texts();
    rv_editor_test_imgui gui;
    const std::string path = root + "/scripts/main.lua";
    rv_editor_code_text t;
    gui.frame([&] {
        t = rv_editor_code_tab_text(path, root, false, 400.0f);
    });
    RV_EDITOR_CHECK(t.tooltip == path);
}

// AC7: the code tile's title holds a path: its tail stays (verified; S4 already makes path titles keep the tail).

RV_EDITOR_TEST(rv_editor_test_code_title_keeps_the_file_name)
{
    load_texts();
    rv_editor_test_imgui gui;
    const std::string title = std::string(rv_editor_text("shell_titles.code_prefix")) + long_path().substr(root.size() + 1) +
        rv_editor_text("shell_titles.modified_marker");
    std::string shown;
    gui.frame([&] {
        shown = rv_editor_pane_header_title(title.c_str(), narrow);
    });
    RV_EDITOR_CHECK(shown.rfind("...", 0) == 0);
    RV_EDITOR_CHECK(ends_with(shown, "engine.cfg" + std::string(rv_editor_text("shell_titles.modified_marker"))));
    RV_EDITOR_CHECK(ends_with(title, shown.substr(3)));
}

// AC12/AC9: swap recovery is a mode: nothing at `none`, a full-tile ask otherwise

RV_EDITOR_TEST(rv_editor_test_code_swap_recovery_is_a_mode)
{
    load_texts();
    rv_editor_test_imgui gui;
    const auto draw = [&](rv_editor_swap_view view, bool with_call) {
        const auto body = [&](rv_editor_probe_record &p) {
            if (with_call) {
                rv_editor_code_swap_place(view, 12345, theme);
            }
            grid(p);
        };
        // The first frame of a new child window records no items: settle it, then record.
        rv_editor_probe_draw(gui, 400.0f, body);
        return rv_editor_probe_draw(gui, 400.0f, body);
    };
    const auto bare = draw(rv_editor_swap_view::none, false);
    const auto none = draw(rv_editor_swap_view::none, true);
    RV_EDITOR_CHECK(grid_difference(bare, none).empty());
    RV_EDITOR_CHECK(bare.content_height == none.content_height);
    for (const auto view : { rv_editor_swap_view::recoverable, rv_editor_swap_view::in_use }) {
        RV_EDITOR_CHECK(outside(draw(view, true), 400.0f).empty());
    }
    const auto recoverable = draw(rv_editor_swap_view::recoverable, true);
    RV_EDITOR_CHECK(find(recoverable, "Recover") != nullptr);
    RV_EDITOR_CHECK(find(recoverable, "Discard") != nullptr);
}

// AC6: Unsaved Changes and Save As in a 15 em tile

// AC8: the Save As path field adapts to the tile and shows the tail

RV_EDITOR_TEST(rv_editor_test_save_as_path_field_fits_the_tile_and_has_the_full_path_tooltip)
{
    load_texts();
    rv_editor_test_imgui gui;
    rv_editor_shell shell;
    shell.save_as_buffer = 1;
    shell.save_as_pane = 1;
    const std::string path = long_path();
    std::snprintf(shell.save_as_path, sizeof(shell.save_as_path), "%s", path.c_str());
    const auto draw = [&] {
        const auto body = [&](rv_editor_probe_record &) {
            rv_editor_shell_ask_save_as(shell, theme);
        };
        // The first frame of a new child window records no items: settle it, then record.
        rv_editor_probe_draw(gui, narrow, body);
        return rv_editor_probe_draw(gui, narrow, body);
    };
    auto r = draw();
    const rv_editor_probe_entry *field = find(r, "##save_as");
    RV_EDITOR_CHECK(field != nullptr);
    if (field == nullptr) {
        return;
    }
    // The field starts inside the tile (a path longer than the tile scrolls inside the card, it is not cut to its tail).
    RV_EDITOR_CHECK(field->x0 < narrow);
    // Hovering the unfocused field shows the whole path in a tooltip.
    ImGui::GetIO().MousePos = ImVec2(field->x0 + 4.0f, (field->y0 + field->y1) / 2.0f);
    draw();
    draw();
    ImGuiWindow *tip = ImGui::FindWindowByName("##Tooltip_00");
    RV_EDITOR_CHECK(tip != nullptr && tip->WasActive);
}

// AC12/AC9: the asks are a mode: the tile shows the ask or the grid, and the ask keeps a constant place

RV_EDITOR_TEST(rv_editor_test_unsaved_ask_is_a_mode_with_a_constant_place)
{
    load_texts();
    rv_editor_test_imgui gui;
    rv_editor_shell shell;
    bool asks = false;
    const auto draw = [&] {
        const auto body = [&](rv_editor_probe_record &p) {
            asks = rv_editor_shell_code_asks(shell, 1, theme);
            if (!asks) {
                grid(p);
            }
        };
        // The first frame of a new child window records no items: settle it, then record.
        rv_editor_probe_draw(gui, 400.0f, body);
        return rv_editor_probe_draw(gui, 400.0f, body);
    };
    const auto bare = rv_editor_probe_draw(gui, 400.0f, [&](rv_editor_probe_record &p) {
        grid(p);
    });
    const auto closed = draw();
    RV_EDITOR_CHECK(!asks);
    RV_EDITOR_CHECK(grid_difference(bare, closed).empty());
    RV_EDITOR_CHECK(bare.content_height == closed.content_height);
    shell.closing = 1;
    const auto asked = draw();
    RV_EDITOR_CHECK(asks);
    shell.saving = true;
    const auto saving = draw();
    RV_EDITOR_CHECK(asks);
    shell.saving = false;
    shell.save_failed.push_back({ 7, "/a/b.lua", false, "disk full, and then the message goes on for a long while" });
    const auto failed = draw();
    RV_EDITOR_CHECK(asks);
    for (const char *b : { "Save", "Discard", "Keep Open" }) {
        RV_EDITOR_CHECK(find(asked, b) != nullptr);
        RV_EDITOR_CHECK(same_rect(asked, saving, b));
        RV_EDITOR_CHECK(same_rect(asked, failed, b));
    }
    RV_EDITOR_CHECK(asked.content_height == saving.content_height);
    RV_EDITOR_CHECK(asked.content_height == failed.content_height);
}

RV_EDITOR_TEST(rv_editor_test_save_as_ask_is_a_mode_with_a_constant_place)
{
    load_texts();
    rv_editor_test_imgui gui;
    rv_editor_shell shell;
    bool asks = false;
    const auto draw = [&] {
        const auto body = [&](rv_editor_probe_record &p) {
            asks = rv_editor_shell_code_asks(shell, 1, theme);
            if (!asks) {
                grid(p);
            }
        };
        // The first frame of a new child window records no items: settle it, then record.
        rv_editor_probe_draw(gui, 400.0f, body);
        return rv_editor_probe_draw(gui, 400.0f, body);
    };
    const auto bare = rv_editor_probe_draw(gui, 400.0f, [&](rv_editor_probe_record &p) {
        grid(p);
    });
    const auto closed = draw();
    RV_EDITOR_CHECK(!asks);
    RV_EDITOR_CHECK(grid_difference(bare, closed).empty());
    shell.save_as_buffer = 1;
    shell.save_as_pane = 1;
    const auto asked = draw();
    RV_EDITOR_CHECK(asks);
    shell.save_as_error = "it is a directory, and the message goes on for a long while";
    const auto failed = draw();
    RV_EDITOR_CHECK(asks);
    for (const char *b : { "Save", "Cancel" }) {
        RV_EDITOR_CHECK(find(asked, b) != nullptr);
        RV_EDITOR_CHECK(same_rect(asked, failed, b));
    }
    RV_EDITOR_CHECK(asked.content_height == failed.content_height);
}

// Review Changes: blocks keep their places between idle, saving and failures within one mode

namespace
{

using leave = rv_editor_shell::rv_editor_leave;

rv_editor_probe_record review(rv_editor_test_imgui &gui, rv_editor_shell &shell, float width)
{
    return rv_editor_probe_draw(gui, width, [&](rv_editor_probe_record &) {
        rv_editor_page_review(shell, theme);
    });
}

// Compares the rects of the named buttons and the content height of three states of one mode.
void review_states_keep_places(leave mode, const std::vector<std::string> &buttons)
{
    load_texts();
    rv_editor_test_imgui gui;
    rv_editor_shell shell;
    shell.leaving = mode;
    const auto idle = review(gui, shell, 400.0f);
    shell.saving = true;
    const auto saving = review(gui, shell, 400.0f);
    shell.saving = false;
    shell.save_failed.push_back({ 7, "/a/b.lua", false, "disk full" });
    shell.save_failed.push_back({ 0, "", false, "nvim said no, and the message goes on for a long while" });
    const auto failed = review(gui, shell, 400.0f);
    for (const std::string &b : buttons) {
        const auto *x = find(idle, b);
        const auto *s = find(saving, b);
        const auto *f = find(failed, b);
        RV_EDITOR_CHECK(x != nullptr && s != nullptr && f != nullptr);
        if (x == nullptr || s == nullptr || f == nullptr) {
            continue;
        }
        RV_EDITOR_CHECK(x->y0 == s->y0 && x->y1 == s->y1);
        RV_EDITOR_CHECK(x->y0 == f->y0 && x->y1 == f->y1);
    }
    RV_EDITOR_CHECK(idle.content_height == saving.content_height);
    RV_EDITOR_CHECK(idle.content_height == failed.content_height);
}

} // namespace

RV_EDITOR_TEST(rv_editor_test_review_changes_quit_keeps_its_blocks_in_place)
{
    review_states_keep_places(leave::quit, { "Save All", "Discard All", "Return" });
}

RV_EDITOR_TEST(rv_editor_test_review_changes_build_keeps_its_blocks_in_place)
{
    load_texts();
    const std::string verb = rv_editor_text("shell_save.build_action");
    review_states_keep_places(leave::build, { "Save and " + verb, "Return" });
}

