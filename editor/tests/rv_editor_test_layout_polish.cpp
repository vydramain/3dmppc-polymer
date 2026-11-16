// Shared layout fixes: heights round up, a shelf keeps its content inside its padding, a disabled button never
// reports focus.

#include "imgui_internal.h"
#include "rv_editor_test.hpp"
#include "rv_editor_test_imgui.hpp"
#include "ui/rv_editor_widget_item.hpp"
#include "ui/rv_editor_widgets.hpp"

using namespace rv_editor;

RV_EDITOR_TEST(rv_editor_test_px_ceil_rounds_up)
{
    RV_EDITOR_CHECK(rv_editor_px_ceil(20.0f) == 20);
    RV_EDITOR_CHECK(rv_editor_px_ceil(20.3f) == 21);
    RV_EDITOR_CHECK(rv_editor_px_ceil(20.99f) == 21);
    RV_EDITOR_CHECK(rv_editor_px_ceil(0.0f) == 0);
}

RV_EDITOR_TEST(rv_editor_test_shelf_content_right_edge_inside_padding)
{
    rv_editor_test::rv_editor_test_imgui gui;
    const rv_editor_theme theme = rv_editor_theme_olive;
    const float pad = static_cast<float>(theme.pad_px * theme.scale);
    gui.frame([&] {
        const float shelf_right = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
        rv_editor_shelf_begin("shelf", theme);
        // What a full-width element sizes itself by.
        const float content_right = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
        RV_EDITOR_CHECK(content_right <= shelf_right - pad);
        rv_editor_shelf_end();
    });
}

RV_EDITOR_TEST(rv_editor_test_disabled_button_with_nav_focus_is_not_focused)
{
    rv_editor_test::rv_editor_test_imgui gui;
    const rv_editor_theme theme = rv_editor_theme_olive;
    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    bool focused = true;
    for (int i = 0; i < 3; ++i) {
        gui.frame([&] {
            ImGui::PushID("b");
            const ImGuiID id = ImGui::GetID("##item");
            ImGui::PopID();
            if (i == 0) {
                ImGui::SetFocusID(id, ImGui::GetCurrentWindow());
            }
            ImGui::PushID("b");
            const rv_editor_item item = rv_editor_item_add("##item", ImVec2(50, 20), { rv_editor_look::live, "why" });
            ImGui::PopID();
            focused = item.focused;
            RV_EDITOR_CHECK(ImGui::GetCurrentContext()->NavId == id || i == 0);
        });
    }
    RV_EDITOR_CHECK(!focused);
}

RV_EDITOR_TEST(rv_editor_test_enabled_button_with_nav_focus_is_focused)
{
    rv_editor_test::rv_editor_test_imgui gui;
    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    bool focused = false;
    for (int i = 0; i < 3; ++i) {
        gui.frame([&] {
            ImGui::PushID("b");
            const ImGuiID id = ImGui::GetID("##item");
            ImGui::PopID();
            if (i == 0) {
                ImGui::SetFocusID(id, ImGui::GetCurrentWindow());
            }
            ImGui::PushID("b");
            focused = rv_editor_item_add("##item", ImVec2(50, 20), {}).focused;
            ImGui::PopID();
        });
    }
    RV_EDITOR_CHECK(focused);
}
