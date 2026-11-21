// S14.1, AC14, AC16, AC18: the mockup layouts as the editor opens them, with all four loaded into one pane registry; the
// helpers the checks share are in rv_editor_test_layout_mockup.hpp.

#include <string>

#include "app/rv_editor_shell.hpp"
#include "rv_editor_test.hpp"
#include "rv_editor_test_imgui.hpp"
#include "rv_editor_test_layout_mockup.hpp"
#include "text/rv_editor_text.hpp"

using namespace rv_editor;
using namespace rv_editor_test;
using namespace rv_editor_test::layout_mockup;

// S14.1, AC14, AC16, AC18: the editor's own start. It loads all four layouts into one pane registry (the product's path:
// rv_editor_shell_load_layouts) and opens on `key`, so a preset's Game is not the registry's first Game pane. In the running
// editor the Scene, Debug and Burn Game tiles stayed at the preset's ratios (the fit looked for the first Game pane, the Code
// layout's, which is not in the shown tree): the picture area must reach its target there as well.
RV_EDITOR_TEST(rv_editor_test_layout_mockup_picture_reaches_the_target_in_the_loaded_editor)
{
    std::string error;
    RV_EDITOR_CHECK(rv_editor_text_load({}, error) == 0);
    rv_editor_test_imgui gui(true);
    const struct {
        const char *key;
        rv_editor_layout_preset preset;
    } starts[] = { { "code", rv_editor_layout_preset::code },
        { "scene", rv_editor_layout_preset::scene },
        { "debug", rv_editor_layout_preset::debug },
        { "burn", rv_editor_layout_preset::burn } };
    for (const auto &start : starts) {
        for (size_t w = 0; w < 2; ++w) {
            view v;
            v.name = preset_name(start.preset);
            v.where = std::string(v.name) + " loaded " + std::to_string(windows[w].w) + "x" + std::to_string(windows[w].h);
            rv_editor_shell_load_layouts(v.shell, {}, start.key);
            v.shell.app.preset = start.preset;
            v.shell.app.project.screen_w = 320;
            v.shell.app.project.screen_h = 240;
            RV_EDITOR_CHECK(say(v, v.shell.active == start.preset, "the editor did not open on the asked layout"));
            const bool steady = run(v, gui, windows[w]);
            RV_EDITOR_CHECK(say(v, steady, "the picture area moved after it settled"));
            const bool ok = picture_is(v, w == 1, start.preset == rv_editor_layout_preset::scene);
            RV_EDITOR_CHECK(ok);
            if (!ok) {
                dump(v);
            }
        }
    }
}
