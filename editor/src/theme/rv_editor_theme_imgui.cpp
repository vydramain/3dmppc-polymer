#include "theme/rv_editor_theme_imgui.hpp"

namespace rv_editor
{

namespace
{

ImVec4 rv_editor_vec(uint32_t rgb, float alpha = 1.0f)
{
    ImVec4 v = ImGui::ColorConvertU32ToFloat4(rv_editor_col(rgb));
    v.w = alpha;
    return v;
}

void rv_editor_theme_colors(const rv_editor_theme &t, ImVec4 *c)
{
    c[ImGuiCol_Text] = rv_editor_vec(t.text);
    c[ImGuiCol_TextDisabled] = rv_editor_vec(t.text_disabled);
    c[ImGuiCol_WindowBg] = rv_editor_vec(t.window);
    c[ImGuiCol_ChildBg] = rv_editor_vec(t.window);
    c[ImGuiCol_PopupBg] = rv_editor_vec(t.window);
    c[ImGuiCol_Border] = rv_editor_vec(t.bevel_lo);
    c[ImGuiCol_BorderShadow] = rv_editor_vec(t.bevel_hi, 0.0f);
    c[ImGuiCol_FrameBg] = rv_editor_vec(t.inset);
    c[ImGuiCol_FrameBgHovered] = rv_editor_vec(t.button);
    c[ImGuiCol_FrameBgActive] = rv_editor_vec(t.inset);
    c[ImGuiCol_TitleBg] = rv_editor_vec(t.dark);
    c[ImGuiCol_TitleBgActive] = rv_editor_vec(t.dark);
    c[ImGuiCol_TitleBgCollapsed] = rv_editor_vec(t.dark);
    c[ImGuiCol_MenuBarBg] = rv_editor_vec(t.window);
    c[ImGuiCol_ScrollbarBg] = rv_editor_vec(t.inset);
    c[ImGuiCol_ScrollbarGrab] = rv_editor_vec(t.button);
    c[ImGuiCol_ScrollbarGrabHovered] = rv_editor_vec(t.bevel_hi);
    c[ImGuiCol_ScrollbarGrabActive] = rv_editor_vec(t.selection);
    c[ImGuiCol_CheckMark] = rv_editor_vec(t.text);
    c[ImGuiCol_CheckboxSelectedBg] = rv_editor_vec(t.inset);
    c[ImGuiCol_SliderGrab] = rv_editor_vec(t.button);
    c[ImGuiCol_SliderGrabActive] = rv_editor_vec(t.selection);
    c[ImGuiCol_Button] = rv_editor_vec(t.button);
    c[ImGuiCol_ButtonHovered] = rv_editor_vec(t.bevel_hi);
    c[ImGuiCol_ButtonActive] = rv_editor_vec(t.inset);
    c[ImGuiCol_Header] = rv_editor_vec(t.selection);
    c[ImGuiCol_HeaderHovered] = rv_editor_vec(t.bevel_hi);
    c[ImGuiCol_HeaderActive] = rv_editor_vec(t.selection);
    c[ImGuiCol_Separator] = rv_editor_vec(t.bevel_lo);
    c[ImGuiCol_SeparatorHovered] = rv_editor_vec(t.selection);
    c[ImGuiCol_SeparatorActive] = rv_editor_vec(t.selection);
    c[ImGuiCol_ResizeGrip] = rv_editor_vec(t.bevel_lo, 0.0f);
    c[ImGuiCol_ResizeGripHovered] = rv_editor_vec(t.selection);
    c[ImGuiCol_ResizeGripActive] = rv_editor_vec(t.selection);
    c[ImGuiCol_InputTextCursor] = rv_editor_vec(t.text_bright);
    // Unselected tabs sit dark; the selected one takes the pane's own colour and
    // a brass overline, so it reads as the front of the stack.
    c[ImGuiCol_TabHovered] = rv_editor_vec(t.button);
    c[ImGuiCol_Tab] = rv_editor_vec(t.inset);
    c[ImGuiCol_TabSelected] = rv_editor_vec(t.window);
    c[ImGuiCol_TabSelectedOverline] = rv_editor_vec(t.selection);
    c[ImGuiCol_TabDimmed] = rv_editor_vec(t.inset);
    c[ImGuiCol_TabDimmedSelected] = rv_editor_vec(t.window);
    c[ImGuiCol_TabDimmedSelectedOverline] = rv_editor_vec(t.bevel_hi);
    c[ImGuiCol_PlotLines] = rv_editor_vec(t.text);
    c[ImGuiCol_PlotLinesHovered] = rv_editor_vec(t.selection);
    c[ImGuiCol_PlotHistogram] = rv_editor_vec(t.selection);
    c[ImGuiCol_PlotHistogramHovered] = rv_editor_vec(t.warning);
    c[ImGuiCol_TableHeaderBg] = rv_editor_vec(t.button);
    c[ImGuiCol_TableBorderStrong] = rv_editor_vec(t.bevel_lo);
    c[ImGuiCol_TableBorderLight] = rv_editor_vec(t.bevel_lo);
    c[ImGuiCol_TableRowBg] = rv_editor_vec(t.inset);
    c[ImGuiCol_TableRowBgAlt] = rv_editor_vec(t.button);
    c[ImGuiCol_TextLink] = rv_editor_vec(t.selection);
    c[ImGuiCol_TextSelectedBg] = rv_editor_vec(t.selection);
    c[ImGuiCol_TreeLines] = rv_editor_vec(t.bevel_hi);
    c[ImGuiCol_DragDropTarget] = rv_editor_vec(t.selection);
    c[ImGuiCol_DragDropTargetBg] = rv_editor_vec(t.selection, 0.25f);
    c[ImGuiCol_UnsavedMarker] = rv_editor_vec(t.warning);
    c[ImGuiCol_NavCursor] = rv_editor_vec(t.selection);
    c[ImGuiCol_NavWindowingHighlight] = rv_editor_vec(t.selection);
    c[ImGuiCol_NavWindowingDimBg] = rv_editor_vec(t.dark, 0.5f);
    c[ImGuiCol_ModalWindowDimBg] = rv_editor_vec(t.dark, 0.5f);
}

} // namespace

void rv_editor_theme_apply(const rv_editor_theme &theme, ImGuiStyle &style)
{
    style = ImGuiStyle();
    rv_editor_theme_colors(theme, style.Colors);

    const float pad = static_cast<float>(theme.pad_px);
    const float bevel = static_cast<float>(theme.bevel_px);
    style.WindowPadding = ImVec2(pad * 2.0f, pad * 2.0f);
    style.FramePadding = ImVec2(pad, pad);
    style.ItemSpacing = ImVec2(pad * 2.0f, pad);
    style.ItemInnerSpacing = ImVec2(pad, pad);
    style.CellPadding = ImVec2(pad, pad / 2.0f);
    style.WindowBorderSize = bevel;
    style.ChildBorderSize = bevel;
    style.PopupBorderSize = bevel;
    style.FrameBorderSize = 0.0f;
    style.TabBorderSize = 0.0f;

    style.WindowRounding = 0.0f;
    style.ChildRounding = 0.0f;
    style.PopupRounding = 0.0f;
    style.FrameRounding = 0.0f;
    style.ScrollbarRounding = 0.0f;
    style.GrabRounding = 0.0f;
    style.TabRounding = 0.0f;
    style.TreeLinesRounding = 0.0f;
    style.MenuItemRounding = 0.0f;
    style.SelectableRounding = 0.0f;
    style.ImageRounding = 0.0f;
    style.DragDropTargetRounding = 0.0f;

    // Pixel art: every edge lands on whole pixels, nothing is smoothed.
    style.AntiAliasedLines = false;
    style.AntiAliasedLinesUseTex = false;
    style.AntiAliasedFill = false;

    style.ScaleAllSizes(static_cast<float>(theme.scale));
}

} // namespace rv_editor
