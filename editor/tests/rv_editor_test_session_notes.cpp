// S8: Session is one content area, the session's facts on top and Notes below it. Notes holds the field and the
// buttons Mark Moment, Capture Frame and Report Issue on its title row, and the timed lines of the session: its start,
// the marks, the stderr of its process and its end with the exit code. Open Full Log is in the end's record.
// Runtime Controls and the Toolchest carry neither the note field nor those buttons (Restart stays in the Toolchest).

#include <chrono>
#include <string>
#include <thread>
#include <vector>

#include "app/rv_editor_app.hpp"
#include "app/rv_editor_app_findings.hpp"
#include "app/rv_editor_shell.hpp"
#include "panes/rv_editor_pane_mark_tools.hpp"
#include "panes/rv_editor_panes.hpp"
#include "rv_editor_test.hpp"
#include "rv_editor_test_pane_state.hpp"
#include "text/rv_editor_text.hpp"
#include "ui/rv_editor_widgets_content.hpp"
#include "theme/rv_editor_theme_imgui.hpp"

using namespace rv_editor;
using namespace rv_editor_test;
using namespace rv_editor_test::pane_state;

namespace
{

constexpr float width = 500.0f;
const rv_editor_theme theme = rv_editor_theme_olive;

const char *const tool_keys[] = { "pane_toolchest.mark_button",
    "pane_toolchest.capture_button",
    "pane_toolchest.report_button" };

// A pane `part` of a label path "a/b/c" is a child window of the content area when it is "##area_" + 8 hex digits.
bool is_area_window(const std::string &label)
{
    const std::string part = label.substr(label.rfind('/') == std::string::npos ? 0 : label.rfind('/') + 1);
    return part.compare(0, 7, "##area_") == 0 && part.size() == 7 + 8;
}

bool is_named(const rv_editor_probe_entry &e, const std::string &name)
{
    return e.label == name || e.label == "##" + name || e.label.compare(0, name.size() + 2, name + "##") == 0;
}

const rv_editor_probe_entry *named(const rv_editor_probe_record &r, const std::string &name)
{
    for (const auto &e : r.entries) {
        if (is_named(e, name)) {
            return &e;
        }
    }
    return nullptr;
}

const rv_editor_probe_entry *note_field(const rv_editor_probe_record &r)
{
    for (const auto &e : r.entries) {
        if (e.label.find("mark_note") != std::string::npos) {
            return &e;
        }
    }
    return nullptr;
}

// The areas of the record: the child windows the content area opens.
std::vector<const rv_editor_probe_entry *> areas(const rv_editor_probe_record &r)
{
    std::vector<const rv_editor_probe_entry *> out;
    for (const auto &e : r.entries) {
        if (is_area_window(e.label)) {
            out.push_back(&e);
        }
    }
    return out;
}

bool has_label_part(const rv_editor_probe_record &r, const char *part)
{
    for (const auto &e : r.entries) {
        if (e.label.find(part) != std::string::npos) {
            return true;
        }
    }
    return false;
}

float text_width(tall_gui &g, const std::string &text)
{
    float w = 0.0f;
    g.gui.frame([&] {
        w = ImGui::CalcTextSize(text.c_str()).x;
    });
    return w;
}

// True when the record has a row that says `text` whole: an item labelled with it, or a text item as wide as it.
bool says(tall_gui &g, const rv_editor_probe_record &r, const std::string &text)
{
    const float need = text_width(g, text) - 0.5f;
    for (const auto &e : r.entries) {
        if (e.label.compare(0, text.size(), text) == 0 || (e.label.empty() && !e.scroll_area && e.x1 - e.x0 >= need)) {
            return true;
        }
    }
    return false;
}

// "HH:MM:SS".
bool is_clock(const std::string &t)
{
    if (t.size() != 8) {
        return false;
    }
    for (size_t i = 0; i < t.size(); ++i) {
        const bool colon = i == 2 || i == 5;
        if (colon ? t[i] != ':' : (t[i] < '0' || t[i] > '9')) {
            return false;
        }
    }
    return true;
}

struct fx {
    tall_gui g;
    rv_editor_app app;
    rv_editor_ui state;
    std::vector<rv_editor_ui_request> requests;
    const rv_editor_ui_port ui{ state, requests };
    fs::path where;
    bool ready = false;

    explicit fx(const char *name,
        bool session = false,
        const std::string &stderr_line = "",
        const std::string &stderr_on_keys = "")
        : where(fresh_dir(name))
    {
        open_project(app, where);
        app.session_profile = "dev";
        ready = true;
        if (session) {
            fake_console_options o;
            o.build_number = 7;
            o.stderr_line = stderr_line;
            o.stderr_on_keys = stderr_on_keys;
            ready = start_fake_session(app, where, o);
            RV_EDITOR_CHECK(ready);
        }
    }
    ~fx()
    {
        std::string e;
        app.session.shutdown(app.log, e);
        remove_dir(where);
    }
    rv_editor_probe_record draw(int frames = 20)
    {
        return settled(
            g,
            width,
            [this] {
                rv_editor_pane_session(app, theme, ui);
            },
            frames);
    }
    // A press and release on the middle of `e`.
    void click(const rv_editor_probe_entry &e)
    {
        ImGuiIO &io = ImGui::GetIO();
        io.AddMousePosEvent((e.x0 + e.x1) / 2.0f, (e.y0 + e.y1) / 2.0f);
        draw(2);
        io.AddMouseButtonEvent(0, true);
        draw(2);
        io.AddMouseButtonEvent(0, false);
        draw(2);
    }
};

} // namespace

// AC9: without a session the pane is one area; its Notes title row carries the field and the three buttons; below it one
// line says there is no session.
RV_EDITOR_TEST(rv_editor_test_session_no_session_is_one_area_with_the_notes_row)
{
    fx f("rv_editor_session_notes_none");
    const auto r = f.draw();
    RV_EDITOR_CHECK(areas(r).size() == 1);
    // The old areas and the old message block are gone.
    RV_EDITOR_CHECK(!has_label_part(r, "facts_area"));
    RV_EDITOR_CHECK(!has_label_part(r, "marks_area"));
    RV_EDITOR_CHECK(!has_label_part(r, "##say"));
    RV_EDITOR_CHECK(!has_label_part(r, "##text_"));
    const auto *title = named(r, "Notes");
    const auto *field = note_field(r);
    RV_EDITOR_CHECK(title != nullptr);
    RV_EDITOR_CHECK(field != nullptr);
    if (title == nullptr || field == nullptr) {
        return;
    }
    for (const char *key : tool_keys) {
        const auto *tile = named(r, rv_editor_text(key));
        RV_EDITOR_CHECK(tile != nullptr);
        if (tile != nullptr) {
            // On the title row with the field, right of it.
            RV_EDITOR_CHECK(tile->y0 >= title->y0 - 0.5f && tile->y1 <= title->y1 + 0.5f);
            RV_EDITOR_CHECK(tile->x0 >= field->x1);
        }
    }
    RV_EDITOR_CHECK(field->y0 >= title->y0 - 0.5f && field->y1 <= title->y1 + 0.5f);
    // Exactly one line under the title row.
    size_t below = 0;
    for (const auto &e : r.entries) {
        if (!e.scroll_area && e.y0 >= title->y1 - 0.5f && e.x1 > e.x0 && e.in_scroll) {
            ++below;
        }
    }
    RV_EDITOR_CHECK(below == 1);
}

RV_EDITOR_TEST(rv_editor_test_session_no_session_line_text)
{
    fx f("rv_editor_session_notes_none_text");
    RV_EDITOR_CHECK(std::string(rv_editor_text("pane_session.no_session_yet")) == "No session yet: Run starts one");
    RV_EDITOR_CHECK(says(f.g, f.draw(), "No session yet: Run starts one"));
}

// AC9: a running session: still one area, the facts on top, then Notes.
RV_EDITOR_TEST(rv_editor_test_session_live_is_one_area_facts_then_notes)
{
    fx f("rv_editor_session_notes_live", true);
    if (!f.ready) {
        return;
    }
    const auto r = f.draw();
    RV_EDITOR_CHECK(areas(r).size() == 1);
    RV_EDITOR_CHECK(!has_label_part(r, "facts_area"));
    RV_EDITOR_CHECK(!has_label_part(r, "marks_area"));
    const auto *title = named(r, "Notes");
    RV_EDITOR_CHECK(title != nullptr);
    if (title == nullptr) {
        return;
    }
    size_t above = 0;
    for (const auto &e : r.entries) {
        if (e.in_scroll && !e.scroll_area && e.y1 <= title->y0 + 0.5f) {
            ++above;
        }
    }
    RV_EDITOR_CHECK(above > 0);
}

// AC10: start, Mark `x`, stop with code 0: three notes in this order, each with "HH:MM:SS".
RV_EDITOR_TEST(rv_editor_test_session_notes_start_mark_end)
{
    fx f("rv_editor_session_notes_chain", true);
    if (!f.ready) {
        return;
    }
    const auto number = f.app.session.number();
    const auto frame = f.app.session.frame();
    RV_EDITOR_CHECK(rv_editor_app_add_mark(f.app, "x") == RV_OK);
    RV_EDITOR_CHECK(end_fake_session(f.app));
    RV_EDITOR_CHECK(f.app.session.exit_status().code == 0);
    const auto notes = rv_editor_session_notes(f.app);
    RV_EDITOR_CHECK(notes.size() == 3);
    if (notes.size() != 3) {
        return;
    }
    for (const auto &n : notes) {
        RV_EDITOR_CHECK(is_clock(n.time));
    }
    RV_EDITOR_CHECK(notes[0].text == "Session #" + std::to_string(number) + " started: build #7, profile dev");
    RV_EDITOR_CHECK(notes[1].text == "Mark, frame " + std::to_string(frame) + ": x");
    RV_EDITOR_CHECK(notes[2].text.find("exit code 0") != std::string::npos);
}

// No session yet: nothing to note.
RV_EDITOR_TEST(rv_editor_test_session_notes_empty_before_any_session)
{
    fx f("rv_editor_session_notes_before");
    RV_EDITOR_CHECK(rv_editor_session_notes(f.app).empty());
}

// AC10: a line the process wrote to stderr is a note.
RV_EDITOR_TEST(rv_editor_test_session_notes_show_the_stderr_lines)
{
    fx f("rv_editor_session_notes_stderr", true, "boom from stderr");
    if (!f.ready) {
        return;
    }
    const auto seen = [&] {
        for (const auto &n : rv_editor_session_notes(f.app)) {
            if (n.text.find("boom from stderr") != std::string::npos && is_clock(n.time)) {
                return true;
            }
        }
        return false;
    };
    RV_EDITOR_CHECK(pump_session(f.app, seen));
}

// R9: the notes go by time, kinds mixed: a stderr line that came before the mark stands before it, one that came after
// stands after it. The clock has whole seconds, so the events are a little over a second apart.
RV_EDITOR_TEST(rv_editor_test_session_notes_go_by_time_not_by_kind)
{
    fx f("rv_editor_session_notes_by_time", true, "early line", "late line");
    if (!f.ready) {
        return;
    }
    const auto has = [&](const char *part) {
        for (const auto &n : rv_editor_session_notes(f.app)) {
            if (n.text.find(part) != std::string::npos) {
                return true;
            }
        }
        return false;
    };
    RV_EDITOR_CHECK(pump_session(f.app, [&] {
        return has("early line");
    }));
    std::this_thread::sleep_for(std::chrono::milliseconds(1100));
    RV_EDITOR_CHECK(rv_editor_app_add_mark(f.app, "middle") == RV_OK);
    std::this_thread::sleep_for(std::chrono::milliseconds(1100));
    RV_EDITOR_CHECK(ask_fake_session(f.app, "keys alpha"));
    RV_EDITOR_CHECK(pump_session(f.app, [&] {
        return has("late line");
    }));
    const auto notes = rv_editor_session_notes(f.app);
    const auto index = [&](const char *part) {
        for (size_t i = 0; i < notes.size(); ++i) {
            if (notes[i].text.find(part) != std::string::npos) {
                return static_cast<int>(i);
            }
        }
        return -1;
    };
    const int early = index("early line");
    const int middle = index("middle");
    const int late = index("late line");
    RV_EDITOR_CHECK(early >= 0 && middle >= 0 && late >= 0);
    RV_EDITOR_CHECK(early < middle);
    RV_EDITOR_CHECK(middle < late);
}

// AC10: Open Full Log is a button in Notes once the session ended and kept a log; a live session has none.
RV_EDITOR_TEST(rv_editor_test_session_open_full_log_is_in_the_notes_of_an_ended_session)
{
    fx f("rv_editor_session_notes_log", true);
    if (!f.ready) {
        return;
    }
    std::string error;
    const fs::path log = f.where / "logs" / "session.log";
    RV_EDITOR_CHECK(f.app.log_files.open(f.app.session.pid(), log, f.app.log, error) == RV_OK);
    const std::string open_log = rv_editor_text("pane_session.open_full_log");
    RV_EDITOR_CHECK(named(f.draw(), open_log) == nullptr);
    RV_EDITOR_CHECK(end_fake_session(f.app));
    const auto r = f.draw();
    const auto *button = named(r, open_log);
    RV_EDITOR_CHECK(button != nullptr);
    RV_EDITOR_CHECK(areas(r).size() == 1);
    if (button == nullptr) {
        return;
    }
    const auto *title = named(r, "Notes");
    RV_EDITOR_CHECK(title != nullptr && button->y0 >= title->y1 - 0.5f);
    RV_EDITOR_CHECK(f.app.open_requests.empty());
    f.click(*button);
    RV_EDITOR_CHECK(f.app.open_requests.size() == 1);
}

// AC11: Mark Moment in Notes asks for a mark, as it did in Runtime Controls.
RV_EDITOR_TEST(rv_editor_test_session_mark_moment_in_notes_asks_for_a_mark)
{
    fx f("rv_editor_session_notes_mark", true);
    if (!f.ready) {
        return;
    }
    const auto r = f.draw();
    const auto *mark = named(r, rv_editor_text("pane_toolchest.mark_button"));
    RV_EDITOR_CHECK(mark != nullptr);
    if (mark == nullptr) {
        return;
    }
    RV_EDITOR_CHECK(!rv_editor_app_request_pending(f.app, rv_editor_pane_request_kind::add_mark));
    f.click(*mark);
    RV_EDITOR_CHECK(rv_editor_app_request_pending(f.app, rv_editor_pane_request_kind::add_mark));
}

// The pane is never narrower than the Notes title row: the title, the field's hint and the three buttons.
RV_EDITOR_TEST(rv_editor_test_session_minimum_holds_the_notes_row)
{
    tall_gui g;
    float need = 0.0f;
    float have = 0.0f;
    g.gui.frame([&] {
        need = rv_editor_content_section_min_width("Notes", rv_editor_mark_tools_min_width(theme, false));
        have = static_cast<float>(rv_editor_session_minimum().w);
    });
    RV_EDITOR_CHECK(have >= need);
}
