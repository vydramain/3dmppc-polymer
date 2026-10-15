// Menus, popups and context menus in the menu style, and menu items that show why they cannot run.

#include "theme/rv_editor_theme_imgui.hpp"
#include "ui/rv_editor_widgets.hpp"

namespace rv_editor
{

namespace
{

// Keeps the style only while the ImGui begin is open; a closed one clears it here.
bool rv_editor_menu_styled_begin(bool open)
{
    if (!open) {
        rv_editor_menu_style_pop();
    }
    return open;
}

} // namespace

bool rv_editor_menu_check(const char *label, const char *shortcut, bool *on, const char *why_not)
{
    const bool clicked = ImGui::MenuItem(label, shortcut, on, why_not == nullptr);
    if (why_not != nullptr && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip("%s", why_not);
    }
    return clicked;
}

bool rv_editor_menu_item(const char *label, const char *shortcut, const char *why_not)
{
    return rv_editor_menu_check(label, shortcut, nullptr, why_not);
}

bool rv_editor_main_menu_bar_begin()
{
    rv_editor_menu_style_push();
    return rv_editor_menu_styled_begin(ImGui::BeginMainMenuBar());
}

void rv_editor_main_menu_bar_end()
{
    ImGui::EndMainMenuBar();
    rv_editor_menu_style_pop();
}

bool rv_editor_menu_bar_begin()
{
    rv_editor_menu_style_push();
    return rv_editor_menu_styled_begin(ImGui::BeginMenuBar());
}

void rv_editor_menu_bar_end()
{
    ImGui::EndMenuBar();
    rv_editor_menu_style_pop();
}

bool rv_editor_menu_begin(const char *label, const char *why_not)
{
    rv_editor_menu_style_push();
    const bool open = ImGui::BeginMenu(label, why_not == nullptr);
    if (why_not != nullptr && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip("%s", why_not);
    }
    return rv_editor_menu_styled_begin(open);
}

void rv_editor_menu_end()
{
    ImGui::EndMenu();
    rv_editor_menu_style_pop();
}

bool rv_editor_popup_begin(const char *id)
{
    rv_editor_menu_style_push();
    return rv_editor_menu_styled_begin(ImGui::BeginPopup(id));
}

void rv_editor_popup_end()
{
    ImGui::EndPopup();
    rv_editor_menu_style_pop();
}

bool rv_editor_context_menu_begin(const char *id)
{
    rv_editor_menu_style_push();
    return rv_editor_menu_styled_begin(ImGui::BeginPopupContextItem(id));
}

void rv_editor_context_menu_end()
{
    ImGui::EndPopup();
    rv_editor_menu_style_pop();
}

bool rv_editor_window_context_menu_begin(const char *id)
{
    rv_editor_menu_style_push();
    return rv_editor_menu_styled_begin(ImGui::BeginPopupContextWindow(id));
}

void rv_editor_window_context_menu_end()
{
    ImGui::EndPopup();
    rv_editor_menu_style_pop();
}

} // namespace rv_editor
