// Assets: the project's resource files as an IRIX Icon Catalog: a
// picture or a kind icon with a short label, or a list with details; a folder
// and a filter. What each file is called on the disc comes from the burner's map
// of the last build, never from a rule of the editor's.

#include "panes/rv_editor_panes.hpp"

#include <algorithm>
#include <cfloat>
#include <chrono>
#include <cmath>
#include <map>
#include <string>
#include <system_error>
#include <vector>

#include "imgui.h"

#include "app/rv_editor_shell.hpp"
#include "build/rv_editor_build_map.hpp"
#include "panes/rv_editor_asset_preview.hpp"
#include "panes/rv_editor_pane_tiled.hpp"
#include "text/rv_editor_text.hpp"
#include "theme/rv_editor_theme_imgui.hpp"
#include "ui/rv_editor_glyphs.hpp"
#include "ui/rv_editor_icons.hpp"
#include "ui/rv_editor_sound.hpp"
#include "ui/rv_editor_widgets.hpp"
#include "ui/rv_editor_widgets_content.hpp"
#include "ui/rv_editor_widgets_message.hpp"
#include "ui/rv_editor_widgets_table.hpp"

namespace rv_editor
{

namespace
{

// Columns of the assets table: file, kind, on disc, size.
constexpr int assets_table_columns = 4;

// What the catalog shows, read again at most once a second, and the map of the build it names.
struct rv_editor_assets_cache {
    std::filesystem::path root;
    std::chrono::steady_clock::time_point read{};
    std::vector<rv_editor_asset> files;
    std::filesystem::path map_path;
    std::filesystem::file_time_type map_time{};
    std::map<std::string, rv_editor_map_entry> map;
    std::map<std::string, rv_editor_icon> pictures; // PNG thumbnails, loaded once
    std::string selected;                           // the rel path shown in the preview strip

    // A sound's duration, read once per selection and kept until the
    // selection or the file's write time moves on.
    std::string sound_facts_for;
    std::filesystem::file_time_type sound_facts_time{};
    double sound_seconds = 0.0;
};

// Assets list refresh rate: rescan at most once per second.
constexpr int refresh_period_seconds = 1;

// Folder and filter field width in font size units.
constexpr float folder_filter_width_em = 9.0f;

// The tallest the selected picture is shown under the resources, in font size units.
constexpr float preview_picture_height_em = 12.0f;

// Horizontal centering: half the gap for centering on both axes (0.5 = symmetric centering).
constexpr float center_offset_ratio = 0.5f;

// Icon cell dimensions in font size units.
constexpr float icon_cell_size_em = 5.5f;
constexpr float icon_glyph_size_em = 3.0f;

// Icon cell: bottom margin added to button height (space for label clearance in pixels).
constexpr float icon_cell_padding_px = 4.0f;

// Icon glyph: initial size in font units before fit scaling.
constexpr float icon_glyph_initial_size_em = 2.0f;

// Icon glyph: max scale as fraction of art width (0.9 * art width to prevent overflow).
constexpr float icon_glyph_scale_max = 0.9f;

// Icon art: top margin in the cell before glyph is drawn (pixels).
constexpr float icon_art_y_offset_px = 2.0f;

// Icon label: offset below glyph and cell height (pixels).
constexpr float icon_label_y_offset_px = 2.0f;

// Icon label: margin kept free at the cell's right edge (pixels).
constexpr float icon_label_margin_px = 4.0f;

// File extensions and special markers.
constexpr std::string_view png_extension = ".png";
constexpr std::string_view scene_toml_suffix = ".scene.toml";

// Selection highlight: alpha opacity of the background rect.
constexpr float selection_highlight_alpha = 0.35f;

// Folder names: excluded from assets or navigation.
constexpr std::string_view build_folder = "build";
constexpr std::string_view src_folder = "src";

// Table and label markers.
constexpr std::string_view entry_not_on_disc = "-";

rv_editor_assets_cache rv_editor_assets;

// Resource files: not code (src/), not hidden, not build output, not the files at the root.
void rv_editor_assets_scan(const std::filesystem::path &root)
{
    rv_editor_assets.files.clear();
    std::error_code ec;
    auto it =
        std::filesystem::recursive_directory_iterator(root, std::filesystem::directory_options::skip_permission_denied, ec);
    for (const auto end = std::filesystem::recursive_directory_iterator(); !ec && it != end; it.increment(ec)) {
        const std::string name = it->path().filename().string();
        std::error_code type_ec;
        if (it->is_directory(type_ec)) {
            if (name.starts_with('.') || name.starts_with(build_folder) || (it.depth() == 0 && name == src_folder)) {
                it.disable_recursion_pending();
            }
            continue;
        }
        if (it.depth() == 0 || name.starts_with('.') || !it->is_regular_file(type_ec)) {
            continue;
        }
        rv_editor_asset a;
        a.path = it->path();
        a.rel = std::filesystem::relative(a.path, root, type_ec).generic_string();
        a.folder = a.rel.substr(0, a.rel.find('/'));
        a.size = it->file_size(type_ec);
        rv_editor_assets.files.push_back(std::move(a));
    }
    std::sort(rv_editor_assets.files.begin(),
        rv_editor_assets.files.end(),
        [](const rv_editor_asset &x, const rv_editor_asset &y) {
            return x.rel < y.rel;
        });
}

void rv_editor_assets_refresh(const rv_editor_app &app)
{
    const auto now = std::chrono::steady_clock::now();
    if (rv_editor_assets.root != app.project.root ||
        now - rv_editor_assets.read > std::chrono::seconds(refresh_period_seconds)) {
        rv_editor_assets.root = app.project.root;
        rv_editor_assets.read = now;
        rv_editor_assets_scan(app.project.root);
    }
    const std::filesystem::path map =
        app.build.last_success() ? rv_editor_build_map_path(app.build.last_success()->dir) : std::filesystem::path();
    std::error_code ec;
    const auto time = map.empty() ? std::filesystem::file_time_type{} : std::filesystem::last_write_time(map, ec);
    if (map != rv_editor_assets.map_path || time != rv_editor_assets.map_time) {
        rv_editor_assets.map_path = map;
        rv_editor_assets.map_time = time;
        rv_editor_assets.map = map.empty() ? decltype(rv_editor_assets.map){} : rv_editor_build_map_read(map);
    }
}

// The disc name, or why there is none yet.
void rv_editor_asset_item(rv_editor_app &app, const rv_editor_asset &a, const rv_editor_ui_port &ui_port)
{
    if (ImGui::IsItemClicked(ImGuiMouseButton_Left) && a.rel != rv_editor_assets.selected) {
        rv_editor_assets.selected = a.rel;
        rv_editor_sound_stop();
    }
    const auto entry = rv_editor_assets.map.find(a.rel);
    if (entry == rv_editor_assets.map.end()) {
        const char *status;
        if (rv_editor_assets.map.empty()) {
            status = rv_editor_text("pane_assets.tooltip_build_once");
        } else {
            status = rv_editor_text("pane_assets.tooltip_manifest");
        }
        const std::string tooltip1 = rv_editor_text_format("pane_assets.tooltip_not_on_disc", std::make_format_args(status));
        rv_editor_item_tooltip((a.rel + "\n" + tooltip1).c_str());
    } else {
        const char *sep;
        if (entry->second.parameter.empty()) {
            sep = "";
        } else {
            sep = rv_editor_text("pane_assets.tooltip_sep");
        }
        const std::string tooltip2 = rv_editor_text_format("pane_assets.tooltip_on_disc",
            std::make_format_args(entry->second.name, entry->second.kind, sep, entry->second.parameter));
        rv_editor_item_tooltip((a.rel + "\n" + tooltip2).c_str());
    }
    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        if (rv_editor_scene_tab_open(app, ui_port, a.path)) {
            return;
        }
        if (a.rel.ends_with(scene_toml_suffix)) {
            rv_editor_app_scene_open(app, a.path);
            ui_port.requests.push_back({ .kind = rv_editor_ui_request_kind::front_scene_tab, .scene_tab_index = 0 });
            app.layout_request = rv_editor_layout_preset::scene;
            app.show_request = rv_editor_pane_kind::scene;
        } else {
            app.layout_request = rv_editor_layout_preset::code;
            app.open_requests.push_back({ a.path, 0 });
        }
    }
}

} // namespace

// Its picture when it is a PNG the editor can read, else an empty icon.
rv_editor_icon rv_editor_asset_picture(SDL_Renderer *renderer, const rv_editor_asset &a)
{
    if (a.path.extension() != png_extension || renderer == nullptr) {
        return {};
    }
    auto it = rv_editor_assets.pictures.find(a.rel);
    if (it == rv_editor_assets.pictures.end()) {
        it = rv_editor_assets.pictures.emplace(a.rel, rv_editor_image_load(renderer, a.path.string())).first;
    }
    return it->second;
}

namespace
{

// The shelf's items: folder, filter, Details and Refresh, with the gap before each.
float rv_editor_assets_shelf_items()
{
    const float gap = ImGui::GetStyle().ItemSpacing.x;
    return 2.0f * ImGui::GetFontSize() * folder_filter_width_em +
        rv_editor_checkbox_width(rv_editor_text("pane_assets.details")) +
        rv_editor_button_width(rv_editor_text("pane_assets.refresh")) + 3.0f * gap;
}

// The box's button rows over the states: a sound's Play and Stop with Add to Disc, or Open Project.
std::vector<std::vector<std::string>> rv_editor_assets_button_rows()
{
    return { { rv_editor_text("asset_preview.button_play"),
                 rv_editor_text("asset_preview.button_stop"),
                 rv_editor_text("asset_preview.button_add_to_disc") },
        { rv_editor_text("panes.open_project_box") } };
}

float rv_editor_assets_content_width()
{
    return std::max(rv_editor_tiled_shelf_width(rv_editor_assets_shelf_items()),
        rv_editor_message_box_min_width(rv_editor_assets_button_rows()));
}

// The folder, the filter, and the view: icons or a list with details; Refresh in Game is always there.
void rv_editor_assets_shelf(rv_editor_app &app, const std::vector<std::string> &folders, const rv_editor_theme &theme)
{
    rv_editor_assets_ui &ui = app.assets_ui;
    rv_editor_shelf_begin("##shelf", theme);
    std::vector<const char *> names;
    for (const std::string &f : folders) {
        names.push_back(f.c_str());
    }
    ui.folder = std::min(ui.folder, static_cast<int>(folders.size()) - 1);
    // Never narrower than the chosen name, its padding and the arrow button.
    const float folder_text = ImGui::CalcTextSize(names[static_cast<size_t>(ui.folder)]).x;
    ImGui::SetNextItemWidth(std::max(ImGui::GetFontSize() * folder_filter_width_em,
        folder_text + 2.0f * ImGui::GetStyle().FramePadding.x + ImGui::GetFrameHeight()));
    rv_editor_dropdown("##folder", &ui.folder, names.data(), static_cast<int>(names.size()), theme);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * folder_filter_width_em);
    rv_editor_text_field("##filter", ui.filter, sizeof(ui.filter), theme);
    rv_editor_item_tooltip(rv_editor_text("pane_assets.filter_tooltip"));
    ImGui::SameLine();
    rv_editor_checkbox(rv_editor_text("pane_assets.details"), &ui.details, theme);
    ImGui::SameLine();
    const bool has_selection = app.project.open && !rv_editor_assets.selected.empty();
    const std::filesystem::path file = has_selection ? app.project.root / rv_editor_assets.selected : std::filesystem::path();
    const rv_editor_change_plan plan = has_selection ? rv_editor_app_change_for(app, file) : rv_editor_change_plan{};
    const bool bake_pending = rv_editor_app_request_pending(app, rv_editor_pane_request_kind::texture_bake);
    const char *why = !has_selection                            ? rv_editor_text("pane_assets.select_first") :
        bake_pending                                            ? rv_editor_text("app_why_not.request_pending") :
        (!app.session.live() || !rv_editor_app_can_reload(app)) ? rv_editor_text("pane_assets.no_console") :
        plan.action != rv_editor_change_action::refresh_texture ? rv_editor_text("pane_assets.not_texture") :
                                                                  rv_editor_app_why_not_reload(app);
    if (rv_editor_button(rv_editor_text("pane_assets.refresh"), theme, { rv_editor_look::live, why })) {
        (void)rv_editor_app_request(app,
            { .kind = rv_editor_pane_request_kind::texture_bake, .path = file, .name = plan.name });
    }
    if (why == nullptr) {
        rv_editor_item_tooltip(rv_editor_text("pane_assets.refresh_tooltip"));
    }
    rv_editor_shelf_end();
}

// The resources as a table of rows.
void rv_editor_assets_table(rv_editor_app &app,
    const std::vector<const rv_editor_asset *> &shown,
    const char *empty_message,
    char *clear_filter,
    const rv_editor_ui_port &ui_port,
    const rv_editor_theme &theme)
{
    const rv_editor_table_column columns[assets_table_columns] = {
        { rv_editor_text("pane_assets.table_file") },
        { rv_editor_text("pane_assets.table_kind"), rv_editor_table_width::fixed },
        { rv_editor_text("pane_assets.table_on_disc") },
        { rv_editor_text("pane_assets.table_size"), rv_editor_table_width::fixed },
    };
    const rv_editor_table_style table_style{ .row_bands = true, .header = true, .sizing = rv_editor_table_sizing::fixed_fit };
    if (!rv_editor_table_begin("##assets", columns, assets_table_columns, table_style)) {
        return;
    }
    if (shown.empty()) {
        rv_editor_table_row();
        rv_editor_table_cell();
        if (rv_editor_content_empty_row(empty_message,
                theme,
                clear_filter != nullptr ? rv_editor_text("pane_assets.clear_filter") : nullptr)) {
            clear_filter[0] = '\0';
        }
    }
    for (const rv_editor_asset *a : shown) {
        rv_editor_table_row();
        rv_editor_table_cell();
        const rv_editor_list_style row_style{ .selected = a->rel == rv_editor_assets.selected,
            .span_columns = true,
            .whole = true };
        rv_editor_list_row(a->rel.c_str(), a->rel.c_str(), theme, row_style);
        rv_editor_asset_item(app, *a, ui_port);
        const auto entry = rv_editor_assets.map.find(a->rel);
        rv_editor_table_cell();
        rv_editor_table_cell_text(entry == rv_editor_assets.map.end() ? entry_not_on_disc.data() : entry->second.kind.c_str(),
            theme);
        rv_editor_table_cell();
        rv_editor_table_cell_text(entry == rv_editor_assets.map.end() ? rv_editor_text("pane_assets.not_on_disc") :
                                                                        entry->second.name.c_str(),
            theme);
        rv_editor_table_cell();
        rv_editor_label(std::to_string(a->size).c_str(), rv_editor_label_kind::normal, theme, rv_editor_label_layout::line);
    }
    rv_editor_table_end();
}

// The resources as icon cells: the picture or the kind's icon over the file's whole name; the cells are as wide as the
// longest name (the area scrolls).
void rv_editor_assets_grid(rv_editor_app &app,
    SDL_Renderer *renderer,
    const std::vector<const rv_editor_asset *> &shown,
    const char *empty_message,
    char *clear_filter,
    const rv_editor_ui_port &ui_port,
    const rv_editor_theme &theme)
{
    if (shown.empty()) {
        if (rv_editor_content_empty_row(empty_message,
                theme,
                clear_filter != nullptr ? rv_editor_text("pane_assets.clear_filter") : nullptr)) {
            clear_filter[0] = '\0';
        }
        return;
    }
    float cell = ImGui::GetFontSize() * icon_cell_size_em;
    for (const rv_editor_asset *a : shown) {
        cell = std::max(cell, ImGui::CalcTextSize(a->path.filename().string().c_str()).x + icon_label_margin_px);
    }
    const float art = ImGui::GetFontSize() * icon_glyph_size_em;
    const int columns = std::max(1, static_cast<int>(ImGui::GetContentRegionAvail().x / cell));
    ImDrawList *dl = ImGui::GetWindowDrawList();
    for (size_t i = 0; i < shown.size(); ++i) {
        const rv_editor_asset &a = *shown[i];
        if (i % static_cast<size_t>(columns) != 0) {
            ImGui::SameLine();
        }
        ImGui::PushID(a.rel.c_str());
        const ImVec2 p0 = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton("##cell", ImVec2(cell, art + ImGui::GetTextLineHeightWithSpacing() + icon_cell_padding_px));
        rv_editor_asset_item(app, a, ui_port);
        if (a.rel == rv_editor_assets.selected) {
            dl->AddRectFilled(p0, ImGui::GetItemRectMax(), ImGui::GetColorU32(ImGuiCol_Header, selection_highlight_alpha));
        }
        if (ImGui::IsItemHovered()) {
            dl->AddRect(p0, ImGui::GetItemRectMax(), rv_editor_col(theme.selection));
        }
        const ImVec2 at(p0.x + (cell - art) * center_offset_ratio, p0.y + icon_art_y_offset_px);
        const rv_editor_icon picture = rv_editor_asset_picture(renderer, a);
        if (picture.id != ImTextureID{}) {
            const float k = std::min(art / static_cast<float>(picture.w), art / static_cast<float>(picture.h));
            const ImVec2 size(picture.w * k, picture.h * k);
            const ImVec2 q0(at.x + (art - size.x) * center_offset_ratio, at.y + (art - size.y) * center_offset_ratio);
            dl->AddImage(picture.id, q0, ImVec2(q0.x + size.x, q0.y + size.y));
        } else {
            // The file's code, sized down from twice the font until it fits the tile's width.
            const char *code = rv_editor_glyph::other_file;
            uint32_t color = 0;
            rv_editor_file_chip(a.path, code, color);
            float size = ImGui::GetFontSize() * icon_glyph_initial_size_em;
            float w = ImGui::GetFont()->CalcTextSizeA(size, FLT_MAX, 0.0f, code).x;
            if (w > art * icon_glyph_scale_max) {
                size *= art * icon_glyph_scale_max / w;
                w = art * icon_glyph_scale_max;
            }
            const ImVec2 glyph_pos(std::floor(at.x + (art - w) * center_offset_ratio),
                std::floor(at.y + (art - size) * center_offset_ratio));
            dl->AddText(ImGui::GetFont(), size, glyph_pos, rv_editor_col(color), code);
        }
        const std::string label = a.path.filename().string();
        const float lw = ImGui::CalcTextSize(label.c_str()).x;
        const ImVec2 label_at(p0.x + (cell - lw) * center_offset_ratio, at.y + art + icon_label_y_offset_px);
        dl->AddText(label_at, rv_editor_col(theme.text), label.c_str());
        ImGui::PopID();
    }
}

// The selected resource's sound duration: read once per selection, refreshed only if the file changed since.
double rv_editor_assets_seconds(const rv_editor_asset &selected)
{
    std::error_code ec;
    const auto write_time = std::filesystem::last_write_time(selected.path, ec);
    if (rv_editor_assets.sound_facts_for != selected.rel || rv_editor_assets.sound_facts_time != write_time) {
        rv_editor_assets.sound_facts_for = selected.rel;
        rv_editor_assets.sound_facts_time = write_time;
        rv_editor_assets.sound_seconds = rv_editor_asset_sound_seconds(selected);
    }
    return rv_editor_assets.sound_seconds;
}

} // namespace

rv_editor_size rv_editor_assets_minimum()
{
    return rv_editor_tiled_minimum(rv_editor_assets_content_width(),
        rv_editor_tiled_shelf_block() + rv_editor_tiled_box_block());
}

void rv_editor_pane_assets(rv_editor_app &app,
    SDL_Renderer *renderer,
    const rv_editor_theme &theme,
    const rv_editor_ui_port &ui_port)
{
    if (app.project.open) {
        rv_editor_assets_refresh(app);
    }
    rv_editor_assets_ui &ui = app.assets_ui;
    std::vector<std::string> folders{ rv_editor_text("pane_assets.all_folders") };
    const std::vector<rv_editor_asset> none;
    const std::vector<rv_editor_asset> &files = app.project.open ? rv_editor_assets.files : none;
    for (const rv_editor_asset &a : files) {
        if (std::find(folders.begin(), folders.end(), a.folder) == folders.end()) {
            folders.push_back(a.folder);
        }
    }
    rv_editor_assets_shelf(app, folders, theme);

    std::vector<const rv_editor_asset *> shown;
    for (const rv_editor_asset &a : files) {
        if ((ui.folder == 0 || a.folder == folders[static_cast<size_t>(ui.folder)]) &&
            (ui.filter[0] == '\0' || a.rel.find(ui.filter) != std::string::npos)) {
            shown.push_back(&a);
        }
    }
    const rv_editor_asset *selected = nullptr;
    for (const rv_editor_asset *a : shown) {
        selected = a->rel == rv_editor_assets.selected ? a : selected;
    }
    const auto selected_entry = selected != nullptr ? rv_editor_assets.map.find(selected->rel) : rv_editor_assets.map.end();
    const rv_editor_map_entry *entry = selected_entry != rv_editor_assets.map.end() ? &selected_entry->second : nullptr;
    const rv_editor_icon picture = selected != nullptr ? rv_editor_asset_picture(renderer, *selected) : rv_editor_icon{};
    const double seconds = selected != nullptr ? rv_editor_assets_seconds(*selected) : 0.0;

    // The box: what is selected and what can be done to it, or no project.
    if (!app.project.open) {
        if (rv_editor_message_box("##note",
                rv_editor_text("panes.no_project_open_message"),
                rv_editor_label_kind::normal,
                { rv_editor_text("panes.open_project_box") },
                theme,
                rv_editor_assets_content_width()) == 0) {
            app.open_folder_request = true;
        }
    } else {
        const rv_editor_asset_view view = rv_editor_asset_preview(selected, entry, picture, seconds, app.project);
        std::vector<rv_editor_state> states(view.buttons.size());
        for (size_t i = 0; i < states.size(); ++i) {
            if (view.actions[i] == rv_editor_asset_action::stop && !rv_editor_sound_playing()) {
                states[i].disabled = rv_editor_text("asset_preview.tooltip_nothing_playing");
            }
        }
        const int pressed = rv_editor_message_box("##note",
            view.text,
            rv_editor_label_kind::normal,
            view.buttons,
            theme,
            rv_editor_assets_content_width(),
            nullptr,
            nullptr,
            nullptr,
            &states);
        if (pressed >= 0 && selected != nullptr) {
            rv_editor_asset_act(view.actions[static_cast<size_t>(pressed)], *selected, app.project);
        }
    }

    (void)rv_editor_tiled_area_begin("##assets_area", rv_editor_assets_content_width(), theme);
    const std::string filtered = rv_editor_text_format("pane_assets.no_match_filter", std::make_format_args(ui.filter));
    const char *empty_message = !app.project.open ? rv_editor_text("panes.no_project_open_message") :
        files.empty()                             ? rv_editor_text("pane_assets.no_resources") :
        ui.filter[0] == '\0'                      ? rv_editor_text("pane_assets.no_match_folder") :
                                                    filtered.c_str();
    char *clear_filter = app.project.open && !files.empty() && ui.filter[0] != '\0' ? ui.filter : nullptr;
    if (ui.details) {
        rv_editor_assets_table(app, shown, empty_message, clear_filter, ui_port, theme);
    } else {
        rv_editor_assets_grid(app, renderer, shown, empty_message, clear_filter, ui_port, theme);
    }
    rv_editor_asset_picture_show(picture, ImGui::GetContentRegionAvail().x, ImGui::GetFontSize() * preview_picture_height_em);
    rv_editor_tiled_area_end(theme);
}

} // namespace rv_editor
