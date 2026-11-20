// Audit leftovers: a disabled button shows its reason through the shared widget, an ask place holds its content, a
// single-tab tile draws its tab, the catalog fits a narrow tile, the candidate's void suffix comes from the texts.

#include <sys/wait.h>
#include <unistd.h>

#include <cfloat>
#include <cstdio>
#include <string>

#include "catalog/rv_editor_catalog.hpp"
#include "app/rv_editor_shell.hpp"
#include "imgui_internal.h"
#include "rv_editor_test.hpp"
#include "rv_editor_test_layout_probe.hpp"
#include "layout/rv_editor_tile.hpp"
#include "text/rv_editor_text.hpp"
#include "theme/rv_editor_theme_imgui.hpp"
#include "ui/rv_editor_widgets.hpp"

using namespace rv_editor;
using namespace rv_editor_test;

namespace
{

constexpr rv_editor_theme theme = rv_editor_theme_olive;

void load_texts()
{
    std::string error;
    RV_EDITOR_CHECK(rv_editor_text_load({}, error) == 0);
}

const rv_editor_probe_entry *find_part(const rv_editor_probe_record &r, const std::string &part)
{
    for (const auto &e : r.entries) {
        if (e.label.find(part) != std::string::npos) {
            return &e;
        }
    }
    return nullptr;
}

} // namespace

// 1: hovering a disabled button past the tooltip delay shows its reason, drawn by the shared widget alone.
RV_EDITOR_TEST(rv_editor_test_disabled_button_shows_its_reason_tooltip)
{
    rv_editor_test_imgui gui;
    ImVec2 mouse(1.0f, 1.0f);
    for (int i = 0; i < 12; ++i) {
        ImGui::GetIO().MousePos = mouse;
        ImGui::GetIO().DeltaTime = 0.1f; // 1.2 s in all, past ImGui's hover delay
        const auto r = rv_editor_probe_draw(gui, 300.0f, [&](auto &p) {
            rv_editor_button("Cancel", theme, { .disabled = "Nothing is running" });
            p.item("btn");
        });
        for (const auto &e : r.entries) {
            if (e.label == "btn") {
                mouse = ImVec2((e.x0 + e.x1) / 2.0f, (e.y0 + e.y1) / 2.0f);
            }
        }
    }
    const ImGuiWindow *tip = ImGui::FindWindowByName("##Tooltip_00");
    RV_EDITOR_CHECK(tip != nullptr && tip->WasActive);
}

// 3: an ask place with `lines` text lines and a button row holds its content.
RV_EDITOR_TEST(rv_editor_test_ask_place_fits_header_lines_and_button_row)
{
    for (int lines = 1; lines <= 6; ++lines) {
        rv_editor_test_imgui gui;
        const auto r = rv_editor_probe_draw(gui, 300.0f, [&](auto &p) {
            if (rv_editor_ask_place_begin("ask", "Unsaved changes", lines, true, theme)) {
                for (int i = 0; i < lines; ++i) {
                    ImGui::TextUnformatted("a line of the ask");
                }
                ImGui::Button("Save");
                p.item("Save");
            }
            rv_editor_ask_place_end();
        });
        const auto *place = find_part(r, "##ask");
        const auto *save = find_part(r, "Save");
        RV_EDITOR_CHECK(place != nullptr && save != nullptr);
        if (place != nullptr && save != nullptr) {
            RV_EDITOR_CHECK(save->y1 <= place->y1);
        }
    }
}

// 4: a tile with one tab draws it in the reserved row, and the content is the same as with two tabs.
RV_EDITOR_TEST(rv_editor_test_single_tab_tile_draws_its_tab)
{
    load_texts();
    rv_editor_test_imgui gui;
    rv_editor_workspace ws;
    const rv_editor_pane_id a = rv_editor_pane_add(ws.panes, rv_editor_pane_kind::output);
    ws.layout = rv_editor_layout_make(a);
    ws.focused_leaf = ws.layout.root;
    ImVec2 size;
    const auto pane = [](void *context, rv_editor_pane_id, rv_editor_pane_kind, const rv_editor_theme &) {
        *static_cast<ImVec2 *>(context) = ImGui::GetWindowSize();
    };
    const auto draw = [&] {
        rv_editor_probe_record r;
        for (int i = 0; i < 3; ++i) {
            r = rv_editor_probe_draw(gui, 400.0f, [&](auto &) {
                std::string error;
                rv_editor_workspace_draw(ws, theme, pane, nullptr, &size, { 0, 0, 400, 300 }, error);
            });
        }
        return r;
    };
    const auto one = draw();
    const ImVec2 one_size = size;
    RV_EDITOR_CHECK(find_part(one, "##tab") != nullptr);
    const rv_editor_pane_id b = rv_editor_pane_add(ws.panes, rv_editor_pane_kind::problems);
    rv_editor_tile_insert(ws.layout, ws.layout.root, b, rv_editor_tile_dock::tab);
    const auto two = draw();
    RV_EDITOR_CHECK(find_part(two, "##tab") != nullptr);
    RV_EDITOR_CHECK(size.x == one_size.x && size.y == one_size.y);
}

// 5: the catalog at 15em has no item past the tile's width (ImGui's Debug window is not the catalog's).
RV_EDITOR_TEST(rv_editor_test_catalog_fits_a_narrow_tile)
{
    load_texts();
    rv_editor_test_imgui gui(true);
    float width = 0.0f;
    gui.frame([&] {
        width = 15.0f * ImGui::GetFontSize();
    });
    int past = 0;
    for (int i = 0; i < 3; ++i) {
        const auto r = rv_editor_probe_draw(
            gui,
            width,
            [&](auto &) {
                rv_editor_catalog_draw(theme);
            },
            1500.0f);
        past = 0;
        for (const auto &e : r.entries) {
            if (e.label.find("Debug") == std::string::npos && e.x1 > width + 0.5f) {
                ++past;
            }
        }
    }
    RV_EDITOR_CHECK(past == 0);
}

// 6: the candidate's void suffix is a text, not a literal in the pane.
RV_EDITOR_TEST(rv_editor_test_candidate_void_suffix_is_a_text)
{
    load_texts();
    const std::string key = "pane_candidate.void_suffix";
    RV_EDITOR_CHECK(std::string(rv_editor_text(key)) != key);
    RV_EDITOR_CHECK(std::string(rv_editor_text(key)).find("void") != std::string::npos);
}

namespace
{

// The minimums of the strip kinds and the game, measured outside a frame (as main does, no current font yet) and then
// inside one; true when both agree. A crash is the child's signal, so the parent sees it as a failed check.
bool minimums_outside_frame_equal_inside()
{
    const rv_editor_pane_kind kinds[] = { rv_editor_pane_kind::controls,
        rv_editor_pane_kind::release_controls,
        rv_editor_pane_kind::toolchest,
        rv_editor_pane_kind::game };
    rv_editor_test_imgui gui;
    rv_editor_size outside[4];
    for (int i = 0; i < 4; ++i) {
        rv_editor_shell shell;
        rv_editor_shell_set_minimum(shell, 1, kinds[i], { 0, 0 });
        outside[i] = shell.ws.minimums[1];
    }
    bool same = true;
    gui.frame([&] {
        for (int i = 0; i < 4; ++i) {
            rv_editor_shell shell;
            rv_editor_shell_set_minimum(shell, 1, kinds[i], { 0, 0 });
            same = same && shell.ws.minimums[1].w == outside[i].w && shell.ws.minimums[1].h == outside[i].h;
        }
    });
    return same;
}

} // namespace

// 0: rv_editor_shell_set_minimum is called by main outside NewFrame/EndFrame; it must not need a current font. Run in a
// child process: a SIGSEGV there fails this check instead of killing the test binary.
RV_EDITOR_TEST(rv_editor_test_set_minimum_works_outside_a_frame)
{
    load_texts();
    std::fflush(nullptr);
    const pid_t pid = fork();
    if (pid == 0) {
        _exit(minimums_outside_frame_equal_inside() ? 0 : 1);
    }
    int status = 0;
    RV_EDITOR_CHECK(pid > 0 && waitpid(pid, &status, 0) == pid);
    RV_EDITOR_CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 0);
}
