// The window with no project open: the Project Catalog. The Toolchest on
// the left, and beside it the page it chose: the recent projects with the selected
// one's card, New Project or Open Project. No page covers another.

#include "app/rv_editor_shell.hpp"

#include <algorithm>
#include <cmath>
#include <string>
#include <system_error>

#include "imgui.h"

#include "panes/rv_editor_pane_tiled.hpp"
#include "project/rv_editor_templates.hpp"
#include "text/rv_editor_text.hpp"
#include "theme/rv_editor_theme_imgui.hpp"
#include "ui/rv_editor_glyphs.hpp"
#include "ui/rv_editor_widgets.hpp"
#include "ui/rv_editor_widgets_message.hpp"
#include "ui/rv_editor_widgets_text.hpp"

namespace rv_editor
{

namespace
{

// Manifest filename identifying a project directory.
constexpr const char *disc_manifest_filename = "disc.toml";

// Frame padding is scaled by this factor for roomier controls on the start screen.
constexpr float frame_padding_scale = 2.0f;

// Start screen padding is scaled by this factor for spacing.
constexpr float pad_scale_factor = 3.0f;

// Toolchest (left panel with command buttons) width in font size units.
constexpr float toolchest_width_em = 18.0f;

// The Toolchest row of the page in front looks pressed.
rv_editor_state rv_editor_start_row(const rv_editor_shell &shell, rv_editor_start_page page)
{
    return { shell.start_page == page ? rv_editor_look::pressed : rv_editor_look::live, nullptr };
}

void rv_editor_start_toolchest(rv_editor_shell &shell, const rv_editor_theme &theme)
{
    rv_editor_pane_header(rv_editor_text("shell_start.toolchest_title"), true, theme);
    if (rv_editor_command_button("##recent",
            rv_editor_glyph::recent,
            theme.code_cyan,
            rv_editor_text("shell_start.recent_projects"),
            rv_editor_text("shell_start.recent_tooltip"),
            theme,
            rv_editor_start_row(shell, rv_editor_start_page::recent))) {
        shell.start_page = rv_editor_start_page::recent;
    }
    if (rv_editor_command_button("##new",
            rv_editor_glyph::new_,
            theme.code_green,
            rv_editor_text("shell_start.new_project"),
            rv_editor_text("shell_start.new_project_tooltip"),
            theme,
            rv_editor_start_row(shell, rv_editor_start_page::new_project))) {
        shell.start_page = rv_editor_start_page::new_project;
    }
    if (rv_editor_command_button("##open",
            rv_editor_glyph::open,
            theme.selection,
            rv_editor_text("shell_start.open_project"),
            rv_editor_text("shell_start.open_project_tooltip"),
            theme,
            rv_editor_start_row(shell, rv_editor_start_page::open_project))) {
        rv_editor_shell_open_project(shell);
    }
    ImGui::Dummy(ImVec2(0.0f, ImGui::GetFrameHeight()));
    if (rv_editor_command_button("##settings",
            rv_editor_glyph::settings,
            theme.code_blue,
            rv_editor_text("shell_start.settings"),
            rv_editor_text("shell_start.settings_tooltip"),
            theme,
            rv_editor_start_row(shell, rv_editor_start_page::settings))) {
        rv_editor_shell_page(shell, rv_editor_start_page::settings);
    }
    if (rv_editor_command_button("##help",
            rv_editor_glyph::help,
            theme.code_yellow,
            rv_editor_text("shell_start.help"),
            rv_editor_text("shell_start.help_tooltip"),
            theme,
            rv_editor_start_row(shell, rv_editor_start_page::help))) {
        rv_editor_shell_page(shell, rv_editor_start_page::help);
    }
    if (rv_editor_command_button("##manual",
            rv_editor_glyph::manual,
            theme.code_yellow,
            rv_editor_text("manual.start_label"),
            rv_editor_text("manual.start_tooltip"),
            theme,
            rv_editor_start_row(shell, rv_editor_start_page::manual))) {
        rv_editor_shell_page(shell, rv_editor_start_page::manual);
    }
}

// Up and the path field, the least the path row is wide, and the Open Project / Cancel row.
constexpr float open_path_min_em = 12.0f;
constexpr float open_area_min_em = 20.0f;

// The rows around the area, apart from the strip: the path row over it, the button row under it, each with its gap.
float rv_editor_open_rows()
{
    return 2.0f * (ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.y);
}

float rv_editor_open_content_width()
{
    const ImGuiStyle &style = ImGui::GetStyle();
    const float path =
        rv_editor_button_width(rv_editor_text("browser.up")) + style.ItemSpacing.x + ImGui::GetFontSize() * open_path_min_em;
    const float buttons = rv_editor_button_width(rv_editor_text("shell_start.open_project")) +
        rv_editor_button_width(rv_editor_text("browser.cancel")) + style.ItemSpacing.x;
    return std::max({ path, buttons, ImGui::GetFontSize() * open_area_min_em, rv_editor_message_strip_min_width({ {} }) });
}

} // namespace

rv_editor_size rv_editor_open_project_minimum()
{
    return rv_editor_tiled_minimum(rv_editor_open_content_width(), rv_editor_tiled_strip_block() + rv_editor_open_rows());
}

void rv_editor_page_open_project(rv_editor_shell &shell, const rv_editor_theme &theme)
{
    rv_editor_browser &b = shell.open_browser;
    if (b.purpose.empty()) {
        const std::filesystem::path from =
            shell.app.project.open ? shell.app.project.root.parent_path() : shell.start_selected.parent_path();
        rv_editor_browser_start(b,
            rv_editor_text("shell_start.choose_game_dir"),
            rv_editor_text("shell_start.open_project"),
            rv_editor_browse_pick::directory,
            from);
    }
    std::error_code ec;
    const std::filesystem::path target = rv_editor_browser_target(b);
    const bool found = std::filesystem::exists(target / disc_manifest_filename, ec);
    const std::string target_path = target.string();
    const std::string found_msg = rv_editor_text_format("shell_start.disc_found_with_path", std::make_format_args(target_path));
    const std::string notfound_msg = rv_editor_text_format("shell_start.no_disc_in_path", std::make_format_args(target_path));
    rv_editor_browser_path_row(b, theme);
    const char *why_not = rv_editor_shell_why_not_project_switch(shell);
    if (why_not == nullptr && !found) {
        why_not = rv_editor_text("shell_start.no_disc_in_this_dir");
    }
    std::filesystem::path picked;
    // The area is as tall as the listing, up to what the tile has; the buttons follow it.
    const ImGuiStyle &style = ImGui::GetStyle();
    const float listing = static_cast<float>(b.entries.size()) * ImGui::GetTextLineHeightWithSpacing() + style.ItemSpacing.y +
        std::floor(style.ScrollbarSize) + 2.0f * style.FramePadding.y;
    const float buttons_row = ImGui::GetFrameHeight() + style.ItemSpacing.y;
    // What the directory is for, then what it holds: a strip under the listing, however long the path.
    const std::string about = b.purpose + "\n" + (found ? found_msg : notfound_msg);
    const float strip_w = std::max(ImGui::GetContentRegionAvail().x, rv_editor_open_content_width());
    const float strip_h = rv_editor_message_strip_height(about, {}, strip_w);
    const float strip_room = strip_h + style.ItemSpacing.y;
    (void)rv_editor_tiled_area_begin("##open_area",
        rv_editor_open_content_width(),
        theme,
        {},
        false,
        std::max(1.0f, std::min(listing, ImGui::GetContentRegionAvail().y - buttons_row - strip_room)),
        strip_h);
    rv_editor_browse_result r = rv_editor_browser_entries(b, why_not, picked, theme);
    rv_editor_tiled_area_end(theme);
    (void)rv_editor_message_strip("##about",
        about,
        found ? rv_editor_label_kind::info : rv_editor_label_kind::error,
        {},
        theme,
        strip_w);
    if (rv_editor_button(b.action.c_str(), theme, { rv_editor_look::live, rv_editor_browser_why_not(b, why_not) })) {
        picked = target;
        r = rv_editor_browse_result::picked;
    }
    ImGui::SameLine();
    if (rv_editor_button(rv_editor_text("browser.cancel"), theme)) {
        r = rv_editor_browse_result::cancelled;
    }
    if (r == rv_editor_browse_result::none) {
        return;
    }
    // Picked or cancelled, the next Open Project starts afresh; without a project
    // the window goes back to the recent projects.
    b = {};
    if (!shell.app.project.open) {
        shell.start_page = rv_editor_start_page::recent;
    }
    if (r == rv_editor_browse_result::picked) {
        rv_editor_shell_request_open(shell, picked);
    } else if (shell.manual.step == rv_editor_manual_step::pick_project) {
        shell.manual.step = rv_editor_manual_step::open_picker;
    }
}

void rv_editor_shell_start_screen(rv_editor_shell &shell, const rv_editor_theme &theme)
{
    rv_editor_pane_header(rv_editor_text("shell_start.catalog_title"), true, theme);
    // Roomier controls here than in the tool panes; the font stays as it is.
    const ImVec2 frame = ImGui::GetStyle().FramePadding;
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(frame.x * frame_padding_scale, frame.y * frame_padding_scale));
    const float pad = static_cast<float>(theme.pad_px) * theme.scale * pad_scale_factor;
    ImGui::SetCursorPos(ImVec2(ImGui::GetCursorPosX() + pad, ImGui::GetCursorPosY() + pad));
    const float column = ImGui::GetFontSize() * toolchest_width_em;
    rv_editor_well_begin("##start_tools", ImVec2(column, -pad), theme);
    rv_editor_start_toolchest(shell, theme);
    rv_editor_well_end();
    ImGui::SameLine(0.0f, pad);
    rv_editor_well_begin("##start_catalog", ImVec2(-pad, -pad), theme);
    // The tabs of a tile name its page; here the page names itself.
    constexpr const char *page_titles[] = { "shell_start.recent_projects",
        "shell_start_new.title",
        "shell_start.open_project",
        "shell_settings.pane_header",
        "page_help.title",
        "manual.title" };
    rv_editor_pane_header(rv_editor_text(page_titles[static_cast<size_t>(shell.start_page)]), true, theme);
    switch (shell.start_page) {
    case rv_editor_start_page::recent:
        rv_editor_page_recent(shell, theme);
        break;
    case rv_editor_start_page::new_project:
        rv_editor_page_new_project(shell, theme);
        break;
    case rv_editor_start_page::open_project:
        rv_editor_page_open_project(shell, theme);
        break;
    case rv_editor_start_page::settings:
        rv_editor_page_settings(shell, theme);
        break;
    case rv_editor_start_page::help:
        rv_editor_page_help(shell, theme);
        break;
    case rv_editor_start_page::manual:
        rv_editor_page_manual(shell, theme);
        break;
    }
    rv_editor_well_end();
    ImGui::PopStyleVar();
}

} // namespace rv_editor
