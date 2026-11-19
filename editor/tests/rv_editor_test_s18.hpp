// Helpers of the S18 tests: the states of the Code tile and of Review Changes that need no running nvim (the asks of
// unsaved files, Save As, a start that failed), and finders for the ask card in a probe record.

#pragma once

#include <cstdio>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "app/rv_editor_shell.hpp"
#include "imgui_internal.h"
#include "rv_editor_test.hpp"
#include "rv_editor_test_fits_whole.hpp"
#include "rv_editor_test_imgui.hpp"
#include "rv_editor_test_layout_probe.hpp"
#include "rv_editor_test_stable_tiles.hpp"
#include "text/rv_editor_text.hpp"
#include "ui/rv_editor_widgets_mode.hpp"

namespace rv_editor_test
{

namespace s18
{

using namespace rv_editor;

inline constexpr rv_editor_pane_id pane = 1;

inline const std::vector<rv_editor_pane_kind> &kinds()
{
    static const std::vector<rv_editor_pane_kind> all = { rv_editor_pane_kind::code, rv_editor_pane_kind::review_changes };
    return all;
}

inline const char *name_of(rv_editor_pane_kind kind)
{
    return kind == rv_editor_pane_kind::code ? "code" : "review_changes";
}

inline void load_texts()
{
    std::string error;
    RV_EDITOR_CHECK(rv_editor_text_load({}, error) == 0);
}

inline std::string words(size_t chars)
{
    std::string s;
    while (s.size() < chars) {
        s += "word" + std::to_string(s.size() % 10) + " ";
    }
    return s;
}

// The button rows of the asks, as the texts give them.
inline std::vector<std::string> unsaved_buttons()
{
    return { rv_editor_text("shell_save.save_button"),
        rv_editor_text("shell_save.discard_button"),
        rv_editor_text("shell_save.keep_open_button") };
}

inline std::vector<std::string> save_as_buttons()
{
    return { rv_editor_text("shell_save.save_button"), rv_editor_text("shell_save.cancel_button") };
}

inline std::vector<std::string> swap_buttons()
{
    return { rv_editor_text("pane_code.swap_recover_button"), rv_editor_text("pane_code.swap_discard_button") };
}

// A shell with the states of one pane kind that need no nvim, in the order of the plan's table. A state sets everything
// it needs, so running one again after the others gives the same screen; the pane is pane 1.
struct fixture {
    rv_editor_test_imgui &gui;
    rv_editor_pane_kind kind;
    rv_editor_shell shell;
    std::vector<std::function<void()>> states;
    size_t first_ask = 0; // the states from here on are asks that replace the tile content

    fixture(rv_editor_test_imgui &g, rv_editor_pane_kind k)
        : gui(g)
        , kind(k)
    {
        if (kind == rv_editor_pane_kind::code) {
            code_states();
        } else {
            review_states();
        }
    }

    void reset()
    {
        shell.closing = rv_editor_tile_none;
        shell.leaving = rv_editor_shell::rv_editor_leave::none;
        shell.saving = false;
        shell.save_failed.clear();
        shell.save_as_buffer = 0;
        shell.save_as_pane = rv_editor_tile_none;
        shell.save_as_path[0] = '\0';
        shell.save_as_error.clear();
        shell.app.nvim.problem.clear();
    }

    void failures()
    {
        shell.save_failed.push_back({ 7, "/a/b.lua", false, "disk full, " + words(300) });
        shell.save_failed.push_back({ 0, "", false, "nvim said no, " + words(300) });
    }

    void save_as(const std::string &path, const std::string &error)
    {
        shell.save_as_buffer = 1;
        shell.save_as_pane = pane;
        std::snprintf(shell.save_as_path, sizeof(shell.save_as_path), "%s", path.c_str());
        shell.save_as_error = error;
    }

    // Code without a running nvim: the start that failed (a long text), then the asks of the tile's file.
    void code_states()
    {
        first_ask = 1;
        states = {
            [this] {
                reset();
                shell.app.nvim.problem = "nvim did not start: " + words(300);
            },
            [this] {
                reset();
                shell.closing = pane;
            },
            [this] {
                reset();
                shell.closing = pane;
                shell.saving = true;
            },
            [this] {
                reset();
                shell.closing = pane;
                failures();
            },
            [this] {
                reset();
                save_as("", "");
            },
            [this] {
                reset();
                save_as("src/" + words(150) + "engine.cfg", "it is a directory, " + words(300));
            },
        };
    }

    // nothing waits, quit, a build ahead, saving, failed, then Save As.
    void review_states()
    {
        using leave = rv_editor_shell::rv_editor_leave;
        first_ask = 5;
        states = {
            [this] {
                reset();
            },
            [this] {
                reset();
                shell.leaving = leave::quit;
            },
            [this] {
                reset();
                shell.leaving = leave::build;
            },
            [this] {
                reset();
                shell.leaving = leave::quit;
                shell.saving = true;
            },
            [this] {
                reset();
                shell.leaving = leave::quit;
                failures();
            },
            [this] {
                reset();
                shell.leaving = leave::quit;
                save_as("", "it is a directory, " + words(300));
            },
        };
    }

    void draw(float width = 600.0f, float height = 500.0f)
    {
        gui.frame(
            [&] {
                rv_editor_shell_pane(&shell, pane, kind, stable_tiles::theme);
            },
            width,
            height);
    }
};

// What a pane callback saw of its body window.
struct seen {
    rv_editor_shell *shell = nullptr;
    std::map<rv_editor_pane_id, ImVec2> size;
    std::map<rv_editor_pane_id, ImVec2> overflow; // drawn content less the room, per axis
};

inline void record_pane(void *context, rv_editor_pane_id id, rv_editor_pane_kind kind, const rv_editor_theme &theme)
{
    auto *s = static_cast<seen *>(context);
    rv_editor_shell_pane(s->shell, id, kind, theme);
    const ImGuiWindow *w = ImGui::GetCurrentWindow();
    s->size[id] = w->Size;
    s->overflow[id] =
        ImVec2(w->DC.CursorMaxPos.x - w->DC.CursorStartPos.x + w->WindowPadding.x * 2.0f - w->InnerRect.GetWidth(),
            w->DC.CursorMaxPos.y - w->DC.CursorStartPos.y + w->WindowPadding.y * 2.0f - w->InnerRect.GetHeight());
}

inline void record_size_only(void *context, rv_editor_pane_id id, rv_editor_pane_kind, const rv_editor_theme &)
{
    static_cast<seen *>(context)->size[id] = ImGui::GetCurrentWindow()->Size;
}

// The minimum the shell would set for pane 1 of `shell` now, measured inside a frame as the shell does.
inline stable_tiles::minimum minimum_now(rv_editor_test_imgui &gui, rv_editor_shell &shell, rv_editor_pane_kind kind)
{
    stable_tiles::minimum result{ 0, 0 };
    gui.frame([&] {
        shell.ws.minimums.clear();
        rv_editor_shell_set_minimum(shell, pane, kind, { 0, 0 });
        const auto it = shell.ws.minimums.find(pane);
        if (it != shell.ws.minimums.end()) {
            result = { static_cast<float>(it->second.w), static_cast<float>(it->second.h) };
        }
    });
    return result;
}

// Draws the pane of `f` after a few settling frames at `width` x `height` and returns the last frame's record, with the
// content region the pane was opened in.
struct drawn {
    rv_editor_probe_record record;
    ImVec2 origin, avail;
};

inline drawn settled(fixture &f, float width, float height)
{
    drawn d;
    for (int i = 0; i < 3; ++i) {
        d.record = rv_editor_probe_draw(
            f.gui,
            width,
            [&](rv_editor_probe_record &) {
                d.origin = ImGui::GetCursorScreenPos();
                d.avail = ImGui::GetContentRegionAvail();
                rv_editor_shell_pane(&f.shell, pane, f.kind, stable_tiles::theme);
            },
            height);
    }
    return d;
}

// The first entry whose visible label is `label`.
inline const rv_editor_probe_entry *find(const rv_editor_probe_record &r, const std::string &label)
{
    for (const auto &e : r.entries) {
        if (e.label.substr(0, e.label.find("##")) == label || e.label.rfind(label + "##", 0) == 0) {
            return &e;
        }
    }
    return nullptr;
}

// The first scroll area: the text area of a card or of a message box.
inline const rv_editor_probe_entry *text_area(const rv_editor_probe_record &r)
{
    for (const auto &e : r.entries) {
        if (e.scroll_area) {
            return &e;
        }
    }
    return nullptr;
}

// The card of an ask: the child window that fills the content region `origin` + `avail` exactly.
inline const rv_editor_probe_entry *card_of(const drawn &d)
{
    for (const auto &e : d.record.entries) {
        if (e.x0 == d.origin.x && e.y0 == d.origin.y && e.x1 == d.origin.x + d.avail.x && e.y1 == d.origin.y + d.avail.y) {
            return &e;
        }
    }
    return nullptr;
}

} // namespace s18

} // namespace rv_editor_test
