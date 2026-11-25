// S13, R13-R17, AC14-AC16, AC18: the four starting layouts follow the approved mockup in a 1280x720 and a 1920x1080 window,
// drawn through the shell's own loop with the editor's real fonts and real minimums (as rv_editor_test_layout_fit does):
// the Game's picture area has its target size, and the tiles stand where the mockup puts them.

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "app/rv_editor_shell.hpp"
#include "imgui_internal.h"
#include "layout/rv_editor_tile.hpp"
#include "rv_editor_test.hpp"
#include "rv_editor_test_imgui.hpp"
#include "rv_editor_test_layout_mockup.hpp"
#include "font/rv_editor_font.hpp"
#include "text/rv_editor_text.hpp"
#include "theme/rv_editor_theme_imgui.hpp"
#include "ui/rv_editor_widgets.hpp"

using namespace rv_editor;
using namespace rv_editor_test;
using namespace rv_editor_test::layout_mockup;

namespace
{

// What the mockup's constants say per window (R13, R14, AC14, AC15): the Game's picture area exactly 400x300 / 640x480 in Code,
// Debug and Burn and 320x240 / 480x360 in Scene; Files 176 / 240 px wide (+-1). S13.1a.2 (the Game column in 672 px) and S13.1b
// (Files, Assets) shrank the minimums so the editor reaches them.
constexpr int32_t files_width[] = { 176, 240 };

int32_t right(const rv_editor_rect &r)
{
    return r.x + r.w;
}
int32_t bottom(const rv_editor_rect &r)
{
    return r.y + r.h;
}
bool near_to(int32_t a, int32_t b, int32_t by = tol)
{
    return std::abs(a - b) <= by;
}

// `next` starts right after `first` along x, with the bar between (0..bar+tol px); and the same along y.
bool after_x(const view &v, const rv_editor_rect &first, const rv_editor_rect &next)
{
    return next.x - right(first) >= 0 && next.x - right(first) <= v.bar + tol;
}
bool after_y(const view &v, const rv_editor_rect &first, const rv_editor_rect &next)
{
    return next.y - bottom(first) >= 0 && next.y - bottom(first) <= v.bar + tol;
}
// The same column: left edge and width.
bool same_column(const rv_editor_rect &a, const rv_editor_rect &b)
{
    return near_to(a.x, b.x) && near_to(a.w, b.w);
}
bool same_row(const rv_editor_rect &a, const rv_editor_rect &b)
{
    return near_to(a.y, b.y) && near_to(a.h, b.h);
}

// Draws the preset in a `win` window through the shell's loop (the fit's step, the real panes' minimums, the Game pane
// measuring its picture) until it settles; false if the picture moved while it held.
void prepare(view &v, rv_editor_layout_preset preset)
{
    rv_editor_shell &shell = v.shell;
    rv_editor_layout_preset_make(preset, shell.ws.panes, shell.ws.layout);
    shell.ws.focused_leaf = shell.ws.layout.root;
    shell.active = preset;
    shell.app.preset = preset;
    const size_t slot = static_cast<size_t>(preset); // the order of rv_editor_workspaces
    shell.game_fit[slot] = rv_editor_preset_fits_game(preset);
    shell.layout_untouched[slot] = true;
    shell.app.project.screen_w = 320;
    shell.app.project.screen_h = 240;
}

bool settle(view &v, rv_editor_test_imgui &gui, rv_editor_layout_preset preset, const rv_editor_size &win)
{
    prepare(v, preset);
    return run(v, gui, win);
}

// Runs `check` on the settled preset in both windows.
template <class F>
void in_both_windows(rv_editor_layout_preset preset, F check)
{
    std::string error;
    RV_EDITOR_CHECK(rv_editor_text_load({}, error) == 0);
    rv_editor_test_imgui gui(true);
    for (size_t w = 0; w < 2; ++w) {
        view v;
        v.name = preset_name(preset);
        v.where = std::string(v.name) + " " + std::to_string(windows[w].w) + "x" + std::to_string(windows[w].h);
        const bool steady = settle(v, gui, preset, windows[w]);
        // The whole tree is in the window: nothing scrolls and no tile is under its minimum.
        RV_EDITOR_CHECK(say(v, v.need.w <= v.work.w && v.need.h <= v.work.h, "the tree does not fit the work area"));
        RV_EDITOR_CHECK(say(v, steady, "the picture area moved after it settled"));
        const bool ok = check(v, w == 1);
        RV_EDITOR_CHECK(ok);
        if (!ok) {
            dump(v);
        }
    }
}

bool edge_is_the_window(const view &v, const rv_editor_rect &r, bool at_right, bool at_bottom)
{
    bool ok = true;
    ok = say(v, !at_right || near_to(right(r), right(v.root)), "a tile's right edge is not the window's") && ok;
    ok = say(v, !at_bottom || near_to(bottom(r), bottom(v.root)), "a tile's bottom edge is not the window's") && ok;
    return ok;
}

} // namespace

// AC14, AC15, R14: Code. Files 176 / 240 wide at the left, the Code in the middle over one tile of Console Output, Inspector
// Report, Terminal and Search Results; right of the Code the Game over Runtime Controls; the picture area 400x300 / 640x480.
RV_EDITOR_TEST(rv_editor_test_layout_mockup_code)
{
    in_both_windows(rv_editor_layout_preset::code, [](const view &v, bool big) {
        bool ok = picture_is(v, big, false);
        const rv_editor_rect files = v.rect(kind::files);
        const rv_editor_rect code = v.rect(kind::code);
        const rv_editor_rect game = v.rect(kind::game);
        const rv_editor_rect controls = v.rect(kind::controls);
        const rv_editor_rect output = v.rect(kind::output);
        char text[96];
        std::snprintf(text, sizeof text, "Files is %d px wide, not %d", files.w, files_width[big ? 1 : 0]);
        ok = say(v, near_to(files.w, files_width[big ? 1 : 0]), text) && ok;
        ok = say(v,
                 near_to(files.x, v.root.x) && near_to(files.y, v.root.y) && near_to(bottom(files), bottom(v.root)),
                 "Files is not the full-height column at the left") &&
            ok;
        ok = say(v, after_x(v, files, code) && near_to(code.y, v.root.y), "the Code is not right of Files at the top") && ok;
        ok = say(v,
                 after_x(v, code, game) && near_to(game.y, v.root.y) && near_to(right(game), right(v.root)),
                 "the Game is not right of the Code, at the top right") &&
            ok;
        ok = say(v, same_column(game, controls) && after_y(v, game, controls), "Runtime Controls is not under the Game") && ok;
        ok = say(v,
                 near_to(code.x, output.x) && near_to(right(game), right(output)) && near_to(bottom(output), bottom(v.root)),
                 "the output tile is not one tile under the Code and the Game") &&
            ok;
        ok = say(v, after_y(v, code, output) && after_y(v, controls, output), "the output tile is not right under them") && ok;
        ok = say(v,
                 v.tabs_are(kind::output, { kind::output, kind::inspector_report, kind::terminal, kind::search }),
                 "the output tile's tabs are not Console Output, Inspector Report, Terminal, Search Results") &&
            ok;
        ok = say(v,
                 v.tabs_are(kind::code, { kind::code }) && v.tabs_are(kind::files, { kind::files }),
                 "Code and Files are not alone in their tiles") &&
            ok;
        return ok;
    });
}

// AC14, AC16, R15: Scene. The Scene at the top left; under it Assets (Assets, Files, Inspector Report) and
// Inspector side by side, equal in width (+-1); at the right, one column top to bottom: Transform Toolchest, Runtime Controls,
// the Game, Hierarchy to the window's bottom; the picture area 320x240 / 480x360.
RV_EDITOR_TEST(rv_editor_test_layout_mockup_scene)
{
    in_both_windows(rv_editor_layout_preset::scene, [](const view &v, bool big) {
        bool ok = picture_is(v, big, true);
        const rv_editor_rect scene = v.rect(kind::scene);
        const rv_editor_rect assets = v.rect(kind::assets);
        const rv_editor_rect inspector = v.rect(kind::inspector);
        const rv_editor_rect toolchest = v.rect(kind::toolchest);
        const rv_editor_rect controls = v.rect(kind::controls);
        const rv_editor_rect game = v.rect(kind::game);
        const rv_editor_rect hierarchy = v.rect(kind::hierarchy);
        ok = say(v, near_to(scene.x, v.root.x) && near_to(scene.y, v.root.y), "the Scene is not at the top left") && ok;
        ok = say(v,
                 near_to(assets.x, scene.x) && after_y(v, scene, assets) && near_to(bottom(assets), bottom(v.root)),
                 "Assets is not under the Scene, down to the window's bottom") &&
            ok;
        ok = say(v,
                 same_row(assets, inspector) && after_x(v, assets, inspector),
                 "Inspector is not right of Assets, in its row") &&
            ok;
        ok =
            say(v, near_to(right(inspector), right(scene)), "Assets and Inspector together are not as wide as the Scene") && ok;
        ok = say(v, near_to(assets.w, inspector.w), "Assets and Inspector are not equal in width") && ok;
        ok = say(v,
                 near_to(toolchest.y, v.root.y) && after_x(v, scene, toolchest) && near_to(right(toolchest), right(v.root)),
                 "the Toolchest is not the top of the right column") &&
            ok;
        ok = say(v,
                 same_column(toolchest, controls) && after_y(v, toolchest, controls),
                 "Runtime Controls is not under the Toolchest") &&
            ok;
        ok = say(v,
                 same_column(controls, game) && after_y(v, controls, game),
                 "the Game is not under Runtime Controls, as wide") &&
            ok;
        ok = say(v, same_column(game, hierarchy) && after_y(v, game, hierarchy), "Hierarchy is not under the Game") && ok;
        ok = edge_is_the_window(v, hierarchy, true, true) && ok;
        ok = say(v,
                 v.tabs_are(kind::assets, { kind::assets, kind::files, kind::inspector_report }),
                 "Assets' tabs are not Assets, Files, Inspector Report") &&
            ok;
        ok = say(v,
                 v.tabs_are(kind::toolchest, { kind::toolchest }) && v.tabs_are(kind::controls, { kind::controls }),
                 "the Toolchest and Runtime Controls are not alone in their tiles") &&
            ok;
        return ok;
    });
}

// AC14, AC18, R17: Debug. Runtime Controls over the Game at the left, Session (with Observe and Test Case) filling the rest of
// the upper row, and along the bottom the full width Runtime Log, Findings, Inspector Report, Session Toolchest; 400x300 /
// 640x480.
RV_EDITOR_TEST(rv_editor_test_layout_mockup_debug)
{
    in_both_windows(rv_editor_layout_preset::debug, [](const view &v, bool big) {
        bool ok = picture_is(v, big, false);
        const rv_editor_rect controls = v.rect(kind::controls);
        const rv_editor_rect game = v.rect(kind::game);
        const rv_editor_rect session = v.rect(kind::session);
        const rv_editor_rect log = v.rect(kind::runtime_log);
        ok =
            say(v, near_to(controls.x, v.root.x) && near_to(controls.y, v.root.y), "Runtime Controls is not at the top left") &&
            ok;
        ok = say(v, same_column(controls, game) && after_y(v, controls, game), "the Game is not under Runtime Controls") && ok;
        ok = say(v,
                 after_x(v, game, session) && near_to(session.y, v.root.y) && near_to(right(session), right(v.root)),
                 "Session does not fill the upper row right of the Game") &&
            ok;
        ok = say(v,
                 near_to(bottom(session), bottom(game)) && after_y(v, session, log) && after_y(v, game, log),
                 "the log is not right under the upper row") &&
            ok;
        ok = say(v,
                 near_to(log.x, v.root.x) && near_to(log.w, v.root.w) && near_to(bottom(log), bottom(v.root)),
                 "the log is not the full width of the window at the bottom") &&
            ok;
        ok = say(v,
                 v.tabs_are(kind::runtime_log, { kind::runtime_log, kind::findings, kind::inspector_report, kind::toolchest }),
                 "the log's tabs are not Runtime Log, Findings, Inspector Report, Session Toolchest") &&
            ok;
        ok = say(v,
                 v.tabs_are(kind::session, { kind::session, kind::observe, kind::test_case }),
                 "Session's tabs are not Session, Observe, Test Case") &&
            ok;
        return ok;
    });
}

// AC14, AC18, R17: Burn. Release Candidate (with Export) at the left, Release Controls over Candidate Playtest at the right,
// and along the bottom the full width Build Log, Runtime Log, Inspector Report; the picture area 400x300 / 640x480.
RV_EDITOR_TEST(rv_editor_test_layout_mockup_burn)
{
    in_both_windows(rv_editor_layout_preset::burn, [](const view &v, bool big) {
        bool ok = picture_is(v, big, false);
        const rv_editor_rect candidate = v.rect(kind::candidate);
        const rv_editor_rect controls = v.rect(kind::release_controls);
        const rv_editor_rect game = v.rect(kind::game);
        const rv_editor_rect log = v.rect(kind::build_log);
        ok = say(v, near_to(candidate.x, v.root.x) && near_to(candidate.y, v.root.y), "the candidate is not at the top left") &&
            ok;
        ok = say(v,
                 after_x(v, candidate, controls) && near_to(controls.y, v.root.y) && near_to(right(controls), right(v.root)),
                 "Release Controls is not the top of the right column") &&
            ok;
        ok = say(v, same_column(controls, game) && after_y(v, controls, game), "the playtest is not under Release Controls") &&
            ok;
        ok = say(v,
                 near_to(bottom(candidate), bottom(game)) && after_y(v, candidate, log) && after_y(v, game, log),
                 "the log is not right under the upper row") &&
            ok;
        ok = say(v,
                 near_to(log.x, v.root.x) && near_to(log.w, v.root.w) && near_to(bottom(log), bottom(v.root)),
                 "the log is not the full width of the window at the bottom") &&
            ok;
        ok = say(v,
                 v.tabs_are(kind::build_log, { kind::build_log, kind::runtime_log, kind::inspector_report }),
                 "the log's tabs are not Build Log, Runtime Log, Inspector Report") &&
            ok;
        ok = say(v,
                 v.tabs_are(kind::candidate, { kind::candidate, kind::export_ }),
                 "the candidate's tabs are not Release Candidate, Export") &&
            ok;
        return ok;
    });
}

// AC14: 1280x720 -> 1920x1080 -> 1280x720 in one shell: at 1920x1080 the picture area is the target (see picture_is), and
// back at 1280x720 it is the target there and what a fresh layout has.
RV_EDITOR_TEST(rv_editor_test_layout_mockup_picture_follows_a_resize_to_the_target)
{
    std::string error;
    RV_EDITOR_CHECK(rv_editor_text_load({}, error) == 0);
    rv_editor_test_imgui gui(true);
    const rv_editor_layout_preset all[] = { rv_editor_layout_preset::code,
        rv_editor_layout_preset::scene,
        rv_editor_layout_preset::debug,
        rv_editor_layout_preset::burn };
    for (const rv_editor_layout_preset preset : all) {
        view fresh;
        fresh.where = std::string(preset_name(preset)) + " fresh";
        (void)settle(fresh, gui, preset, windows[0]);
        view v;
        v.where = std::string(preset_name(preset)) + " 1280x720";
        RV_EDITOR_CHECK(settle(v, gui, preset, windows[0]));
        // The same shell, the window changes: the draws go on with the tree the first window left.
        RV_EDITOR_CHECK(run(v, gui, windows[1]));
        v.where = std::string(preset_name(preset)) + " resized to 1920x1080";
        RV_EDITOR_CHECK(picture_is(v, true, preset == rv_editor_layout_preset::scene));
        RV_EDITOR_CHECK(run(v, gui, windows[0]));
        v.where = std::string(preset_name(preset)) + " resized back";
        RV_EDITOR_CHECK(picture_is(v, false, preset == rv_editor_layout_preset::scene));
        char text[96];
        std::snprintf(text,
            sizeof text,
            "back at 1280x720 the area is %dx%d, a fresh layout has %dx%d",
            v.picture.w,
            v.picture.h,
            fresh.picture.w,
            fresh.picture.h);
        v.where = std::string(preset_name(preset)) + " resized back";
        RV_EDITOR_CHECK(say(v, v.picture.w == fresh.picture.w && v.picture.h == fresh.picture.h, text));
    }
}
