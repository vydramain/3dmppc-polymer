#include "app/rv_editor_shell.hpp"
#include "imgui_internal.h"
#include "pdk/rv_err.h"
#include "pdklib/rv_version/rv_version.hpp"
#include "prefs/rv_editor_prefs.hpp"
#include "rv_editor_test.hpp"
#include "rv_editor_test_imgui.hpp"
#include "text/rv_editor_text.hpp"
#include "theme/rv_editor_theme_imgui.hpp"
#include "ui/rv_editor_widgets.hpp"

using namespace rv_editor;
using namespace rv_editor_test;

RV_EDITOR_TEST(legacy_view_preferences_keep_their_choices_and_save_main_font_size_independently)
{
    const std::string legacy_view = std::string("3dmppc-editor-view ") + rv_pdklib::rv_version_str + R"(
code-font large
game-scale 2x
workspace burn
ui-scale 3
vsync off
fps-limit 75
)";

    rv_editor_prefs prefs = rv_editor_prefs_read(legacy_view);
    RV_EDITOR_CHECK(prefs.main_size == rv_editor_main_size::normal);
    RV_EDITOR_CHECK(prefs.code_size == rv_editor_code_size::large);
    RV_EDITOR_CHECK(prefs.game_scale == rv_editor_game_scale::x2);
    RV_EDITOR_CHECK(prefs.workspace == "burn");
    RV_EDITOR_CHECK(prefs.ui_scale == 2.0f);
    RV_EDITOR_CHECK(!prefs.vsync);
    RV_EDITOR_CHECK(prefs.fps_limit == 75);

    prefs.main_size = rv_editor_main_size::large;
    prefs.code_size = rv_editor_code_size::small;
    const rv_editor_prefs reloaded = rv_editor_prefs_read(rv_editor_prefs_write(prefs));
    RV_EDITOR_CHECK(reloaded.main_size == rv_editor_main_size::large);
    RV_EDITOR_CHECK(reloaded.code_size == rv_editor_code_size::small);
    RV_EDITOR_CHECK(reloaded.game_scale == rv_editor_game_scale::x2);
    RV_EDITOR_CHECK(reloaded.workspace == "burn");
    RV_EDITOR_CHECK(reloaded.ui_scale == 2.0f);
    RV_EDITOR_CHECK(!reloaded.vsync);
    RV_EDITOR_CHECK(reloaded.fps_limit == 75);
}

namespace
{

struct font_metrics {
    float main_height = 0.0f;
    float code_height = 0.0f;
    ImVec2 button;
};

font_metrics
measure_fonts(rv_editor_test_imgui &gui, float ui_scale, rv_editor_main_size main_size, rv_editor_code_size code_size)
{
    ImFontAtlas &atlas = *ImGui::GetIO().Fonts;
    rv_editor_theme theme = rv_editor_theme_olive;
    theme.scale = ui_scale;
    rv_editor_theme_apply(theme, ImGui::GetStyle());
    const int added = rv_editor_fonts_add(atlas, ui_scale, main_size);
    RV_EDITOR_CHECK(added == RV_OK);
    if (added != RV_OK) {
        return {};
    }

    ImGui::GetStyle().FontSizeBase = rv_editor_font_ui()->LegacySize;
    ImFontAtlasUpdateNewFrame(&atlas, atlas.Builder == nullptr ? 0 : atlas.Builder->FrameCount + 1, true);
    rv_editor_font_code_size_set(code_size);

    font_metrics metrics;
    gui.frame([&] {
        constexpr const char *label = "Project settings";
        metrics.main_height = ImGui::GetFontSize();
        const ImVec2 label_size = ImGui::CalcTextSize(label);
        rv_editor_button(label, theme);
        metrics.button = ImGui::GetItemRectSize();
        RV_EDITOR_CHECK(metrics.button.x >= label_size.x && metrics.button.y >= label_size.y);

        rv_editor_font_code_push();
        metrics.code_height = ImGui::GetFontSize();
        rv_editor_font_code_pop();
        RV_EDITOR_CHECK(ImGui::GetFontSize() == metrics.main_height);
    });
    return metrics;
}

} // namespace

RV_EDITOR_TEST(main_and_code_font_sizes_change_independently_at_each_ui_scale)
{
    rv_editor_test_imgui gui(true);
    const rv_editor_code_size previous_code = rv_editor_font_code_size();
    for (const float ui_scale : { 1.0f, 1.5f, 2.0f }) {
        const auto original = measure_fonts(gui, ui_scale, rv_editor_main_size::normal, rv_editor_code_size::small);
        const auto larger_main = measure_fonts(gui, ui_scale, rv_editor_main_size::large, rv_editor_code_size::small);
        const auto larger_both = measure_fonts(gui, ui_scale, rv_editor_main_size::large, rv_editor_code_size::large);

        RV_EDITOR_CHECK(original.main_height == 8.0f * ui_scale);
        RV_EDITOR_CHECK(larger_main.main_height > original.main_height);
        RV_EDITOR_CHECK(larger_main.button.x > original.button.x && larger_main.button.y > original.button.y);
        RV_EDITOR_CHECK(larger_main.code_height == original.code_height);
        RV_EDITOR_CHECK(larger_both.code_height > larger_main.code_height);
        RV_EDITOR_CHECK(larger_both.main_height == larger_main.main_height);
        RV_EDITOR_CHECK(larger_both.button.x == larger_main.button.x && larger_both.button.y == larger_main.button.y);
    }

    RV_EDITOR_CHECK(rv_editor_fonts_add(*ImGui::GetIO().Fonts, 1.0f) == RV_OK);
    rv_editor_font_code_size_set(previous_code);
}

namespace
{

void check_main_font_switch_between_frames(bool workspace_drawn)
{
    std::string error;
    RV_EDITOR_CHECK(rv_editor_text_load({}, error) == RV_OK);
    rv_editor_test_imgui gui(true);
    rv_editor_shell shell;
    rv_editor_layout_preset_make(rv_editor_layout_preset::code, shell.ws.panes, shell.ws.layout);
    rv_editor_theme theme = rv_editor_theme_olive;
    theme.scale = 1.5f;
    const rv_editor_code_size code_size = rv_editor_font_code_size();
    const auto normal = measure_fonts(gui, theme.scale, rv_editor_main_size::normal, code_size);

    if (workspace_drawn) {
        rv_editor_shell_update(shell, false);
        gui.frame(
            [&] {
                RV_EDITOR_CHECK(rv_editor_workspace_draw(
                                    shell.ws,
                                    theme,
                                    [](void *, rv_editor_pane_id, rv_editor_pane_kind, const rv_editor_theme &) {},
                                    nullptr,
                                    nullptr,
                                    { 0, 24, 1920, 1032 },
                                    error) == RV_OK);
            },
            1920.0f,
            1080.0f);
        RV_EDITOR_CHECK(shell.ws.body_chrome.w > 0);
    }

    ImFontAtlas &atlas = *ImGui::GetIO().Fonts;
    RV_EDITOR_CHECK(rv_editor_fonts_add(atlas, theme.scale, rv_editor_main_size::large) == RV_OK);
    rv_editor_theme_apply(theme, ImGui::GetStyle());
    ImGui::GetStyle().FontSizeBase = rv_editor_font_ui()->LegacySize;
    const float large_height = rv_editor_font_ui()->LegacySize;
    ImFontAtlasUpdateNewFrame(&atlas, atlas.Builder->FrameCount + 1, true);

    // The editor measures the current layout before beginning the next frame.
    rv_editor_shell_update(shell, false);

    gui.frame([&] {
        RV_EDITOR_CHECK(ImGui::GetFontSize() == large_height);
        rv_editor_button("Project settings", theme);
        const ImVec2 large_button = ImGui::GetItemRectSize();
        RV_EDITOR_CHECK(large_button.x > normal.button.x && large_button.y > normal.button.y);

        rv_editor_font_code_push();
        RV_EDITOR_CHECK(ImGui::GetFontSize() == normal.code_height);
        rv_editor_font_code_pop();
    });
    RV_EDITOR_CHECK(rv_editor_fonts_add(atlas, 1.0f) == RV_OK);
}

} // namespace

RV_EDITOR_TEST(main_font_switch_survives_pane_measurement_between_frames)
{
    check_main_font_switch_between_frames(false);
}

RV_EDITOR_TEST(main_font_switch_survives_refitting_visible_control_strips)
{
    check_main_font_switch_between_frames(true);
}
