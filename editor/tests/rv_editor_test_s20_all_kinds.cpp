// S20, R1/R3, AC1: the S1 run over all 34 pane kinds, in the base state of a fresh shell: one minimum per kind, the
// pane whole in a tile of that minimum, and the body of every tile without scrolling at the minimum (no kind is left
// with a scrolling body).

#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "app/rv_editor_shell.hpp"
#include "imgui_internal.h"
#include "rv_editor_test.hpp"
#include "rv_editor_test_fits_whole.hpp"
#include "rv_editor_test_imgui.hpp"
#include "rv_editor_test_stable_tiles.hpp"
#include "text/rv_editor_text.hpp"

using namespace rv_editor;
using namespace rv_editor_test;

namespace
{

constexpr rv_editor_pane_id pane = 1;

// In the order of rv_editor_pane_kind.
const char *const kind_names[] = { "empty",
    "catalog",
    "project",
    "files",
    "assets",
    "scene",
    "hierarchy",
    "inspector",
    "game",
    "code",
    "controls",
    "run_config",
    "output",
    "terminal",
    "problems",
    "search",
    "toolchest",
    "runtime_log",
    "build_log",
    "observe",
    "findings",
    "candidate",
    "release_controls",
    "build_result",
    "checks",
    "session",
    "test_case",
    "new_project",
    "open_project",
    "settings",
    "help",
    "review_changes",
    "manual",
    "export_" };

std::vector<rv_editor_pane_kind> all_kinds()
{
    std::vector<rv_editor_pane_kind> all;
    for (uint32_t k = 0; k <= static_cast<uint32_t>(rv_editor_pane_kind_last); ++k) {
        all.push_back(static_cast<rv_editor_pane_kind>(k));
    }
    return all;
}

const char *name_of(rv_editor_pane_kind kind)
{
    return kind_names[static_cast<uint32_t>(kind)];
}

void load_texts()
{
    std::string error;
    RV_EDITOR_CHECK(rv_editor_text_load({}, error) == 0);
}

struct seen {
    rv_editor_shell *shell = nullptr;
    ImVec2 size;
    ImVec2 overflow; // drawn content less the room, per axis
    bool scrollbar = false;
    bool scroll_area = false;
};

const char *last_part(const char *name)
{
    const char *slash = std::strrchr(name, '/');
    return slash != nullptr ? slash + 1 : name;
}

// True when the tile's body is a scroll area (rv_editor_scroll_begin: a "##scroll" right in the leaf's window) and not
// the plain "##pane" child of a converted kind.
bool body_is_a_scroll_area()
{
    for (const ImGuiWindow *w : ImGui::GetCurrentContext()->Windows) {
        if (w->Active && w->ParentWindow != nullptr && std::strncmp(last_part(w->Name), "##scroll", 8) == 0 &&
            std::strncmp(last_part(w->ParentWindow->Name), "##leaf", 6) == 0) {
            return true;
        }
    }
    return false;
}

void record_pane(void *context, rv_editor_pane_id id, rv_editor_pane_kind kind, const rv_editor_theme &theme)
{
    auto *s = static_cast<seen *>(context);
    rv_editor_shell_pane(s->shell, id, kind, theme);
    const ImGuiWindow *w = ImGui::GetCurrentWindow();
    s->size = w->Size;
    s->scrollbar = w->ScrollbarY || w->ScrollbarX;
    s->scroll_area = body_is_a_scroll_area();
    s->overflow = ImVec2(w->DC.CursorMaxPos.x - w->DC.CursorStartPos.x + w->WindowPadding.x * 2.0f - w->InnerRect.GetWidth(),
        w->DC.CursorMaxPos.y - w->DC.CursorStartPos.y + w->WindowPadding.y * 2.0f - w->InnerRect.GetHeight());
}

} // namespace

// AC1: every kind has a minimum of its own, and in the base state its pane is whole in a tile of that minimum.
RV_EDITOR_TEST(rv_editor_test_s20_every_kind_fits_whole_at_its_minimum)
{
    load_texts();
    rv_editor_test_imgui gui(true);
    for (const rv_editor_pane_kind kind : all_kinds()) {
        const auto min = stable_tiles::pane_minimum(gui, kind);
        if (min.w <= 0.0f || min.h <= 0.0f) {
            std::fprintf(stderr, "  %s: no minimum of its own (%.0f x %.0f)\n", name_of(kind), min.w, min.h);
        }
        RV_EDITOR_CHECK(min.w > 0.0f && min.h > 0.0f);
        rv_editor_shell shell;
        const std::string bad = rv_editor_check_pane_states(gui, shell, kind, { [] {} }, min);
        if (!bad.empty()) {
            std::fprintf(stderr, "  %s at %.0fx%.0f: %s\n", name_of(kind), min.w, min.h, bad.c_str());
        }
        RV_EDITOR_CHECK(bad.empty());
    }
}

// R3, AC1: the body of the tile (the "##pane" window) of every kind is drawn at the tile's minimum with no content past
// its edges and no scroll bar; what is longer scrolls inside the pane's own content area.
RV_EDITOR_TEST(rv_editor_test_s20_no_tile_body_scrolls_at_the_minimum)
{
    load_texts();
    rv_editor_test_imgui gui(true);
    for (const rv_editor_pane_kind kind : all_kinds()) {
        rv_editor_shell shell;
        rv_editor_pane_add(shell.ws.panes, rv_editor_pane_kind::empty); // pane 0: the tile's pane is 1
        RV_EDITOR_CHECK(rv_editor_pane_add(shell.ws.panes, kind) == pane);
        shell.ws.layout = rv_editor_layout_make(pane);
        shell.ws.focused_leaf = shell.ws.layout.root;
        seen s;
        s.shell = &shell;
        for (int frame = 0; frame < 4; ++frame) {
            // A window smaller than any minimum: the tile takes its minimum and the body is what is left of it.
            gui.frame(
                [&] {
                    shell.ws.minimums.clear();
                    rv_editor_shell_set_minimum(shell, pane, kind, { 0, 0 });
                    std::string error;
                    rv_editor_workspace_draw(shell.ws, stable_tiles::theme, record_pane, nullptr, &s, { 0, 0, 20, 20 }, error);
                },
                20.0f,
                20.0f);
        }
        if (s.scroll_area) {
            std::fprintf(stderr, "  %s: the body of the tile is a scroll area\n", name_of(kind));
        }
        RV_EDITOR_CHECK(!s.scroll_area);
        if (s.scrollbar || s.overflow.x > 0.5f || s.overflow.y > 0.5f) {
            std::fprintf(stderr,
                "  %s: body %.0fx%.0f scrolls (bar %d, content past it by %.1f x %.1f px)\n",
                name_of(kind),
                s.size.x,
                s.size.y,
                s.scrollbar ? 1 : 0,
                s.overflow.x,
                s.overflow.y);
        }
        RV_EDITOR_CHECK(!s.scrollbar);
        RV_EDITOR_CHECK(s.overflow.x <= 0.5f && s.overflow.y <= 0.5f);
    }
}
