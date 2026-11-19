// The transport strip (Runtime Controls): one row of fixed height whose button size follows only the width, a
// minimum that holds the longest message, the same rectangles in every state.

#include <sys/stat.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "app/rv_editor_shell.hpp"
#include "panes/rv_editor_pane_controls.hpp"
#include "rv_editor_test.hpp"
#include "rv_editor_test_fits_whole.hpp"
#include "rv_editor_test_imgui.hpp"
#include "rv_editor_test_layout_probe.hpp"
#include "rv_editor_test_pane_state.hpp"
#include "rv_editor_test_stable_tiles.hpp"
#include "text/rv_editor_text.hpp"
#include "ui/rv_editor_widgets.hpp"

using namespace rv_editor;
using namespace rv_editor_test;
using namespace rv_editor_test::stable_tiles;

namespace
{

constexpr auto controls = rv_editor_pane_kind::controls;
constexpr float labels_from = 640.0f; // R14: buttons carry labels from this width, by width alone

// The longest message a state can show: build number up to 6 digits, step up to 99/99, name up to 16 characters.
const std::string longest_message = "Build #999999 step 99/99: " + std::string(16, 'x');

rv_editor_probe_record draw_settled(rv_editor_test_imgui &gui, rv_editor_shell &shell, float width, float height = 300.0f)
{
    const auto body = [&](rv_editor_probe_record &) {
        rv_editor_shell_pane(&shell, 1, controls, theme);
    };
    rv_editor_probe_draw(gui, width, body, height);
    return rv_editor_probe_draw(gui, width, body, height);
}

const rv_editor_probe_entry *find(const rv_editor_probe_record &r, const char *label)
{
    for (const auto &e : r.entries) {
        // Small tiles carry the name only as an id (`##name`), large ones show it.
        if (e.label == label || e.label == std::string("##") + label) {
            return &e;
        }
    }
    return nullptr;
}

const char *const button_keys[] = { "widgets_status.button_build",
    "widgets_status.button_run",
    "widgets_status.button_pause",
    "widgets_status.button_step",
    "widgets_status.button_stop",
    "widgets_status.button_reload" };

// The six transport buttons in the idle pane; null where one is missing.
std::vector<const rv_editor_probe_entry *> buttons(const rv_editor_probe_record &r)
{
    std::vector<const rv_editor_probe_entry *> out;
    for (const char *key : button_keys) {
        out.push_back(find(r, rv_editor_text(key)));
    }
    return out;
}

float ui_font_size()
{
    return ImGui::GetIO().Fonts->Fonts[0]->LegacySize;
}

float min_width(rv_editor_test_imgui &gui)
{
    float w = 0.0f;
    gui.frame([&] {
        w = rv_editor_transport_min_width();
    });
    return w;
}

float strip_height(rv_editor_test_imgui &gui, float width)
{
    float h = 0.0f;
    gui.frame([&] {
        h = rv_editor_transport_height(width);
    });
    return h;
}

struct scratch {
    std::filesystem::path path = std::filesystem::temp_directory_path() / "rv_editor_transport_s7";
    scratch()
    {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
        std::filesystem::create_directories(path / "run", ec);
        std::filesystem::create_directories(path / "pause", ec);
    }
    ~scratch()
    {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    }
};

void script_build(rv_editor_app &app, const std::filesystem::path &where, const std::string &body)
{
    std::string guard_error;
    rv_editor_guard_start(guard_error);
    const auto script = where / "burner.sh";
    std::ofstream(script) << "#!/bin/sh\n" << body;
    chmod(script.c_str(), 0700);
    app.tools.burner.path = script;
    app.tools.baker.path = script;
    std::string error;
    RV_EDITOR_CHECK(app.build.start(app.project, app.tools, app.log, error) == RV_OK);
}

// The five states of AC15, each in a shell of its own: idle, building (a step with a 16 character name), running,
// paused, failed.
struct strip_states {
    scratch dir;
    std::vector<std::unique_ptr<rv_editor_shell>> shells;
    strip_states()
    {
        for (int i = 0; i < 5; ++i) {
            shells.push_back(std::make_unique<rv_editor_shell>());
            open_project(shells.back()->app, dir.path);
        }
        script_build(shells[1]->app,
            dir.path,
            "echo 'mppcburner: [plan] a b c' >&2\necho 'mppcburner: [2/3] xxxxxxxxxxxxxxxx' >&2\nsleep 30\n");
        std::string error;
        for (int i = 0; i < 100 && shells[1]->app.build.progress().runs.empty(); ++i) {
            shells[1]->app.build.update(shells[1]->app.log, error);
            usleep(10000);
        }
        RV_EDITOR_CHECK(pane_state::start_fake_session(shells[2]->app, dir.path / "run", {}));
        pane_state::fake_console_options paused;
        paused.mode = "paused";
        RV_EDITOR_CHECK(pane_state::start_fake_session(shells[3]->app, dir.path / "pause", paused));
        RV_EDITOR_CHECK(shells[3]->app.session.state() == rv_editor_run_state::paused);
        script_build(shells[4]->app, dir.path, "exit 1\n");
        for (int i = 0; i < 200 && shells[4]->app.build.busy(); ++i) {
            shells[4]->app.build.update(shells[4]->app.log, error);
            usleep(10000);
        }
        RV_EDITOR_CHECK(shells[4]->app.build.state() == rv_editor_build_state::failed);
    }
    ~strip_states()
    {
        pane_state::end_fake_session(shells[2]->app);
        pane_state::end_fake_session(shells[3]->app);
    }
};

} // namespace

// R14, R5: the minimum is computed from the template at its limits, never from the state: six small buttons, the
// slot and the mark note, and the longest message are all inside it.
RV_EDITOR_TEST(rv_editor_test_transport_min_width_holds_the_longest_message)
{
    std::string error;
    RV_EDITOR_CHECK(rv_editor_text_load({}, error) == 0);
    rv_editor_test_imgui gui;
    gui.frame([&] {
        const float w = rv_editor_transport_min_width();
        const ImGuiStyle &style = ImGui::GetStyle();
        const float buttons_w = 6.0f * ImGui::GetFrameHeight() + 5.0f * style.ItemSpacing.x;
        const float message_w = ImGui::CalcTextSize(longest_message.c_str()).x;
        // The right column holds the slot and the note field at its hint's width; the mark tiles are not in the strip.
        const float slot_w = rv_editor_controls_action_slot_width(theme) + style.ItemSpacing.x +
            ImGui::CalcTextSize(rv_editor_text("pane_toolchest.mark_placeholder")).x + 2.0f * style.FramePadding.x;
        RV_EDITOR_CHECK(w >= buttons_w + style.ItemSpacing.x + message_w);
        RV_EDITOR_CHECK(w >= buttons_w + style.ItemSpacing.x + slot_w);
    });
}

// The shell's minimum for the pane is the strip's, in both directions.
RV_EDITOR_TEST(rv_editor_test_transport_pane_minimum_is_the_strip_minimum)
{
    std::string error;
    RV_EDITOR_CHECK(rv_editor_text_load({}, error) == 0);
    rv_editor_test_imgui gui;
    const float w = min_width(gui);
    RV_EDITOR_CHECK(w > 0.0f);
    if (w <= 0.0f) {
        return;
    }
    const minimum min = pane_minimum(gui, controls);
    RV_EDITOR_CHECK(std::fabs(min.w - w) <= 1.0f);
    RV_EDITOR_CHECK(std::fabs(min.h - strip_height(gui, w)) <= 1.0f);
}

// AC15: the tile is as high as the strip at this width, with no empty space under it; large buttons make it taller.
RV_EDITOR_TEST(rv_editor_test_transport_height_is_the_strip_height_at_every_width)
{
    std::string error;
    RV_EDITOR_CHECK(rv_editor_text_load({}, error) == 0);
    rv_editor_test_imgui gui;
    scratch dir;
    rv_editor_shell shell;
    open_project(shell.app, dir.path);
    const float min = min_width(gui);
    RV_EDITOR_CHECK(min > 0.0f);
    if (min <= 0.0f) {
        return;
    }
    float last = 0.0f;
    for (const float width : { min, labels_from - 1.0f, labels_from, 900.0f, 1280.0f }) {
        const float h = strip_height(gui, width);
        const float drawn = draw_settled(gui, shell, width, 400.0f).content_height;
        if (std::fabs(drawn - h) > 1.0f) {
            std::fprintf(stderr, "  width %.0f: strip draws %.1f, transport_height says %.1f\n", width, drawn, h);
        }
        RV_EDITOR_CHECK(h > 0.0f);
        RV_EDITOR_CHECK(std::fabs(drawn - h) <= 1.0f);
        RV_EDITOR_CHECK(h >= last); // a wider tile never gets lower
        last = h;
    }
    RV_EDITOR_CHECK(strip_height(gui, labels_from) > strip_height(gui, labels_from - 1.0f));
    // The height depends on the width alone: the same at 900 and 1280.
    RV_EDITOR_CHECK(strip_height(gui, 900.0f) == strip_height(gui, 1280.0f));
}

// R14: one row, six buttons in order, the action slot after them; under 640 px small tiles, from 640 px large ones with
// a label, whatever the state; never a medium tile and never a wrapped row.
RV_EDITOR_TEST(rv_editor_test_transport_button_size_follows_the_width_alone)
{
    std::string error;
    RV_EDITOR_CHECK(rv_editor_text_load({}, error) == 0);
    rv_editor_test_imgui gui;
    scratch dir;
    rv_editor_shell shell;
    open_project(shell.app, dir.path);
    float large_side = 0.0f;
    float small_side = 0.0f;
    gui.frame([&] {
        large_side = rv_editor_tile_large_side(ImGui::GetIO().Fonts->Fonts[0], ui_font_size());
        small_side = ImGui::GetFrameHeight();
    });
    const std::string cancel = rv_editor_text("panes.cancel_build_button");
    const float min = min_width(gui);
    RV_EDITOR_CHECK(min > 0.0f);
    if (min <= 0.0f) {
        return;
    }
    for (const float width : { min, 400.0f, labels_from - 1.0f, labels_from, 800.0f, 1280.0f }) {
        if (width < min) {
            continue;
        }
        // From 640 px the large side is what the panel leaves beside the message column (the same reserve the panel
        // passes), never past the natural large side and always bigger than a small one.
        float side = small_side;
        if (width >= labels_from) {
            gui.frame([&] {
                side = rv_editor_transport_side(width, rv_editor_transport_column_min_width());
            });
            RV_EDITOR_CHECK(side <= large_side + 0.5f);
            RV_EDITOR_CHECK(side > small_side);
        }
        const auto r = draw_settled(gui, shell, width, 400.0f);
        const auto b = buttons(r);
        for (size_t i = 0; i < b.size(); ++i) {
            RV_EDITOR_CHECK(b[i] != nullptr);
            if (b[i] == nullptr) {
                continue;
            }
            if (std::fabs((b[i]->x1 - b[i]->x0) - side) > 0.5f || std::fabs((b[i]->y1 - b[i]->y0) - side) > 0.5f) {
                std::fprintf(stderr,
                    "  width %.0f: button %zu is %.1fx%.1f, expected %.1f square\n",
                    width,
                    i,
                    b[i]->x1 - b[i]->x0,
                    b[i]->y1 - b[i]->y0,
                    side);
            }
            RV_EDITOR_CHECK(std::fabs((b[i]->x1 - b[i]->x0) - side) <= 0.5f);
            RV_EDITOR_CHECK(std::fabs((b[i]->y1 - b[i]->y0) - side) <= 0.5f);
            const bool labelled = b[i]->label.rfind("##", 0) != 0;
            RV_EDITOR_CHECK(labelled == (width >= labels_from)); // no visible label on small tiles, one on large
            RV_EDITOR_CHECK(b[i]->y0 == b[0]->y0);               // one row, no flow
            if (i > 0 && b[i - 1] != nullptr) {
                RV_EDITOR_CHECK(b[i]->x0 >= b[i - 1]->x1);
            }
        }
        const auto *slot = find(r, cancel.c_str());
        RV_EDITOR_CHECK(slot != nullptr);
        if (slot != nullptr && b.back() != nullptr) {
            RV_EDITOR_CHECK(slot->x0 >= b.back()->x1);
        }
    }
}

// AC15: five states, one width (compared in order: Run and Resume have different ids), the same rectangle for every button, the
// Cancel Build slot, the mark note and the message line.
RV_EDITOR_TEST(rv_editor_test_transport_states_keep_every_rectangle)
{
    std::string error;
    RV_EDITOR_CHECK(rv_editor_text_load({}, error) == 0);
    rv_editor_test_imgui gui;
    strip_states states;
    const float min = min_width(gui);
    RV_EDITOR_CHECK(min > 0.0f);
    if (min <= 0.0f) {
        return;
    }
    for (const float width : { min, labels_from - 1.0f, labels_from, 900.0f }) {
        const auto base = draw_settled(gui, *states.shells[0], width, 400.0f);
        RV_EDITOR_CHECK(!base.entries.empty());
        for (size_t i = 1; i < states.shells.size(); ++i) {
            const std::string d = same_rects(base, draw_settled(gui, *states.shells[i], width, 400.0f));
            if (!d.empty()) {
                std::fprintf(stderr, "  width %.0f, state %zu: %s\n", width, i, d.c_str());
            }
            RV_EDITOR_CHECK(d.empty());
        }
    }
}

// AC1, AC15: at the minimum width and at 640 px, in the tile as high as the strip, every state fits whole.
RV_EDITOR_TEST(rv_editor_test_transport_fits_whole_in_every_state)
{
    std::string error;
    RV_EDITOR_CHECK(rv_editor_text_load({}, error) == 0);
    rv_editor_test_imgui gui;
    strip_states states;
    const float min = min_width(gui);
    RV_EDITOR_CHECK(min > 0.0f);
    if (min <= 0.0f) {
        return;
    }
    for (const float width : { std::ceil(min), labels_from, 1280.0f }) {
        const minimum tile{ width, std::ceil(strip_height(gui, width)) };
        for (size_t i = 0; i < states.shells.size(); ++i) {
            rv_editor_shell &shell = *states.shells[i];
            const std::string bad = rv_editor_check_pane_states(gui, shell, controls, { [] {} }, tile);
            if (!bad.empty()) {
                std::fprintf(stderr, "  width %.0f, state %zu: %s\n", width, i, bad.c_str());
            }
            RV_EDITOR_CHECK(bad.empty());
        }
    }
    // One pixel under the minimum width the strip no longer fits.
    const minimum narrow{ std::ceil(min) - 1.0f, std::ceil(strip_height(gui, min)) };
    RV_EDITOR_CHECK(!rv_editor_check_pane_states(gui, *states.shells[0], controls, { [] {} }, narrow).empty());
}

// R2, R14: the right column is as wide as the longest message at every width around the label threshold (the build
// number and the step cannot be forged here, so the width of the message row is checked against the template).
RV_EDITOR_TEST(rv_editor_test_transport_message_row_holds_the_longest_message)
{
    std::string error;
    RV_EDITOR_CHECK(rv_editor_text_load({}, error) == 0);
    rv_editor_test_imgui gui;
    strip_states states;
    const float min = std::ceil(min_width(gui));
    RV_EDITOR_CHECK(min > 0.0f);
    if (min <= 0.0f) {
        return;
    }
    float need = 0.0f;
    gui.frame([&] {
        need = ImGui::CalcTextSize(longest_message.c_str()).x;
    });
    for (const float width : { min, labels_from - 1.0f, labels_from, labels_from + 1.0f, 900.0f, 1280.0f }) {
        rv_editor_shell &shell = *states.shells[1];
        const auto r = draw_settled(gui, shell, width, 400.0f);
        const rv_editor_probe_entry *message = nullptr;
        for (const auto &e : r.entries) {
            if (e.label.find("##message") != std::string::npos) {
                message = &e;
            }
        }
        RV_EDITOR_CHECK(message != nullptr);
        if (message != nullptr) {
            if (message->x1 - message->x0 < need) {
                std::fprintf(stderr,
                    "  width %.0f: message row %.1f px, longest message %.1f px\n",
                    width,
                    message->x1 - message->x0,
                    need);
            }
            RV_EDITOR_CHECK(message->x1 - message->x0 >= need);
        }
        const minimum tile{ width, std::ceil(strip_height(gui, width)) };
        const std::string bad = rv_editor_check_pane_states(gui, shell, controls, { [] {} }, tile);
        if (!bad.empty()) {
            std::fprintf(stderr, "  width %.0f: %s\n", width, bad.c_str());
        }
        RV_EDITOR_CHECK(bad.empty());
    }
}

namespace
{

const char *const mark_keys[] = { "pane_toolchest.mark_button",
    "pane_toolchest.capture_button",
    "pane_toolchest.report_button" };

bool has_label(const rv_editor_probe_record &r, const std::string &part)
{
    for (const auto &e : r.entries) {
        if (e.label.find(part) != std::string::npos) {
            return true;
        }
    }
    return false;
}

} // namespace

// R14 (user decision): Mark, Capture and Report are not in Runtime Controls at any width; the note field is.
RV_EDITOR_TEST(rv_editor_test_transport_has_no_mark_tiles_but_the_note_field)
{
    std::string error;
    RV_EDITOR_CHECK(rv_editor_text_load({}, error) == 0);
    rv_editor_test_imgui gui;
    scratch dir;
    rv_editor_shell shell;
    open_project(shell.app, dir.path);
    // Fixed widths, not the strip's minimum, so the test does not depend on that function; 400 is above any minimum.
    for (const float width : { 400.0f, labels_from - 1.0f, labels_from, 900.0f, 1280.0f }) {
        const auto r = draw_settled(gui, shell, width, 400.0f);
        for (const char *key : mark_keys) {
            const std::string name = rv_editor_text(key);
            if (find(r, name.c_str()) != nullptr) {
                std::fprintf(stderr, "  width %.0f: '%s' is in the strip\n", width, name.c_str());
            }
            RV_EDITOR_CHECK(find(r, name.c_str()) == nullptr);
        }
        RV_EDITOR_CHECK(has_label(r, "mark_note"));
    }
}

// R14: the note field shares the row with the Cancel Build slot, the message line is under both, and both rows are
// inside the tile's height, large buttons or small.
RV_EDITOR_TEST(rv_editor_test_transport_right_column_is_two_rows)
{
    std::string error;
    RV_EDITOR_CHECK(rv_editor_text_load({}, error) == 0);
    rv_editor_test_imgui gui;
    scratch dir;
    rv_editor_shell shell;
    open_project(shell.app, dir.path);
    const std::string cancel = rv_editor_text("panes.cancel_build_button");
    for (const float width : { 400.0f, labels_from, 900.0f, 1280.0f }) {
        const auto r = draw_settled(gui, shell, width, 400.0f);
        const auto *slot = find(r, cancel.c_str());
        const rv_editor_probe_entry *note = nullptr;
        const rv_editor_probe_entry *message = nullptr;
        for (const auto &e : r.entries) {
            if (e.label.find("mark_note") != std::string::npos) {
                note = &e;
            } else if (e.label.find("message") != std::string::npos) {
                message = &e;
            }
        }
        RV_EDITOR_CHECK(slot != nullptr);
        RV_EDITOR_CHECK(note != nullptr);
        RV_EDITOR_CHECK(message != nullptr);
        if (slot == nullptr || note == nullptr || message == nullptr) {
            continue;
        }
        const float height = std::max(strip_height(gui, width), 1.0f);
        if (note->y0 != slot->y0 || message->y0 < slot->y1) {
            std::fprintf(stderr,
                "  width %.0f: slot y %.1f, note y %.1f, message y %.1f\n",
                width,
                slot->y0,
                note->y0,
                message->y0);
        }
        RV_EDITOR_CHECK(std::fabs(note->y0 - slot->y0) <= 0.5f);
        RV_EDITOR_CHECK(note->x0 >= slot->x1);
        RV_EDITOR_CHECK(message->y0 >= std::max(slot->y1, note->y1));
        RV_EDITOR_CHECK(message->y1 <= height + 0.5f);
        RV_EDITOR_CHECK(note->y1 <= height + 0.5f);
    }
}

// R14: the tiles stay in the Toolchest.
RV_EDITOR_TEST(rv_editor_test_toolchest_keeps_mark_capture_report)
{
    std::string error;
    RV_EDITOR_CHECK(rv_editor_text_load({}, error) == 0);
    rv_editor_test_imgui gui;
    scratch dir;
    rv_editor_shell shell;
    open_project(shell.app, dir.path);
    shell.app.preset = rv_editor_layout_preset::debug; // the Toolchest shows the mark row in Debug
    const auto body = [&](rv_editor_probe_record &) {
        rv_editor_shell_pane(&shell, 1, rv_editor_pane_kind::toolchest, theme);
    };
    rv_editor_probe_draw(gui, 800.0f, body, 300.0f);
    const auto r = rv_editor_probe_draw(gui, 800.0f, body, 300.0f);
    for (const char *key : mark_keys) {
        const std::string name = rv_editor_text(key);
        RV_EDITOR_CHECK(find(r, name.c_str()) != nullptr);
    }
    RV_EDITOR_CHECK(has_label(r, "mark_note"));
}
