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

#include "build/rv_editor_build_map.hpp"
#include "text/rv_editor_text.hpp"
#include "panes/rv_editor_asset_preview.hpp"
#include "theme/rv_editor_theme_imgui.hpp"
#include "ui/rv_editor_glyphs.hpp"
#include "ui/rv_editor_icons.hpp"
#include "ui/rv_editor_sound.hpp"
#include "ui/rv_editor_widgets.hpp"

namespace rv_editor
{

namespace
{

// Columns of the assets table: file, kind, on disc, size.
constexpr int assets_table_columns = 4;

// What the catalog shows, read again at most once a second, and the map of the build it names.
struct rv_editor_assets_cache
{
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

// Minimum width for side-by-side layout: preview fits beside list only if wider.
constexpr float side_by_side_threshold_em = 28.0f;

// Preview pane size constraints in em for wide layout: 1/3 of width, clamped.
constexpr float preview_width_min_em = 14.0f;
constexpr float preview_width_max_em = 32.0f;

// Preview pane height constraints in em: narrow layout only, clamped.
constexpr float preview_height_min_em = 6.0f;
constexpr float preview_height_max_em = 14.0f;

// Preview height in narrow layout: 40% of available height before clamping.
constexpr float preview_narrow_height_ratio = 0.4f;

// Preview width divisor: preview initially occupies 1/3 of available width.
constexpr float preview_width_divisor = 3.0f;

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

// Icon label: margin subtracted from cell width while truncating (pixels).
constexpr float icon_label_margin_px = 4.0f;

// Label truncation: minimum chars before shortening with ellipsis.
constexpr size_t label_ellipsis_min_chars = 3;

// File extensions and special markers.
constexpr std::string_view png_extension = ".png";
constexpr std::string_view wav_extension = ".wav";
constexpr std::string_view pcm_extension = ".pcm";
constexpr std::string_view scene_toml_suffix = ".scene.toml";
constexpr std::string_view label_ellipsis = "...";

// Label ellipsis: chars erased per iteration (one char + old ellipsis length).
constexpr size_t label_ellipsis_erase_count = label_ellipsis.size() + 1;

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
    auto it = std::filesystem::recursive_directory_iterator(root, std::filesystem::directory_options::skip_permission_denied,
        ec);
    for (const auto end = std::filesystem::recursive_directory_iterator(); !ec && it != end; it.increment(ec)) {
        const std::string name = it->path().filename().string();
        std::error_code type_ec;
        if (it->is_directory(type_ec)) {
            if (name.starts_with('.') || name.starts_with(build_folder) ||
                (it.depth() == 0 && name == src_folder)) {
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
    std::sort(rv_editor_assets.files.begin(), rv_editor_assets.files.end(),
        [](const rv_editor_asset &x, const rv_editor_asset &y) { return x.rel < y.rel; });
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
    const std::filesystem::path map = app.build.last_success() ? rv_editor_build_map_path(app.build.last_success()->dir)
                                                               : std::filesystem::path();
    std::error_code ec;
    const auto time = map.empty() ? std::filesystem::file_time_type{} : std::filesystem::last_write_time(map, ec);
    if (map != rv_editor_assets.map_path || time != rv_editor_assets.map_time) {
        rv_editor_assets.map_path = map;
        rv_editor_assets.map_time = time;
        rv_editor_assets.map = map.empty() ? decltype(rv_editor_assets.map){} : rv_editor_build_map_read(map);
    }
}

// The disc name, or why there is none yet.
void rv_editor_asset_item(rv_editor_app &app, const rv_editor_asset &a)
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
        const std::string tooltip1 = rv_editor_text_format("pane_assets.tooltip_not_on_disc",
            std::make_format_args(status));
        ImGui::SetItemTooltip("%s\n%s", a.rel.c_str(), tooltip1.c_str());
    } else {
        const char *sep;
        if (entry->second.parameter.empty()) {
            sep = "";
        } else {
            sep = rv_editor_text("pane_assets.tooltip_sep");
        }
        const std::string tooltip2 = rv_editor_text_format("pane_assets.tooltip_on_disc",
            std::make_format_args(entry->second.name, entry->second.kind, sep, entry->second.parameter));
        ImGui::SetItemTooltip("%s\n%s", a.rel.c_str(), tooltip2.c_str());
    }
    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        const std::string ext = a.path.extension().string();
        if (ext == png_extension || ext == wav_extension || ext == pcm_extension) {
            rv_editor_app_scene_tab_open(app, a.path);
            app.layout_request = rv_editor_layout_preset::scene;
            app.show_request = rv_editor_pane_kind::scene;
        } else if (a.rel.ends_with(scene_toml_suffix)) {
            rv_editor_app_scene_open(app, a.path);
            app.scene_tabs.front = 0;
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
    if (a.path.extension() != ".png" || renderer == nullptr) {
        return {};
    }
    auto it = rv_editor_assets.pictures.find(a.rel);
    if (it == rv_editor_assets.pictures.end()) {
        it = rv_editor_assets.pictures.emplace(a.rel, rv_editor_image_load(renderer, a.path.string())).first;
    }
    return it->second;
}

void rv_editor_pane_assets(rv_editor_app &app, SDL_Renderer *renderer, const rv_editor_theme &theme)
{
    if (!app.project.open) {
        rv_editor_open_project_row(app, theme);
        return;
    }
    rv_editor_assets_refresh(app);
    rv_editor_assets_ui &ui = app.assets_ui;

    // The folder, the filter, and the view: icons or a list with details.
    rv_editor_shelf_begin("##shelf", theme);
    std::vector<std::string> folders{ rv_editor_text("pane_assets.all_folders") };
    for (const rv_editor_asset &a : rv_editor_assets.files) {
        if (std::find(folders.begin(), folders.end(), a.folder) == folders.end()) {
            folders.push_back(a.folder);
        }
    }
    std::vector<const char *> names;
    for (const std::string &f : folders) {
        names.push_back(f.c_str());
    }
    ui.folder = std::min(ui.folder, static_cast<int>(folders.size()) - 1);
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * folder_filter_width_em);
    rv_editor_dropdown("##folder", &ui.folder, names.data(), static_cast<int>(names.size()), theme);
    rv_editor_flow(ImGui::GetFontSize() * folder_filter_width_em);
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * folder_filter_width_em);
    rv_editor_text_field("##filter", ui.filter, sizeof(ui.filter), theme);
    ImGui::SetItemTooltip("%s", rv_editor_text("pane_assets.filter_tooltip"));
    const char *details_text = rv_editor_text("pane_assets.details");
    rv_editor_flow(rv_editor_checkbox_width(details_text));
    rv_editor_checkbox(details_text, &ui.details, theme);
    if (!rv_editor_assets.selected.empty()) {
        const std::filesystem::path file = app.project.root / rv_editor_assets.selected;
        const rv_editor_change_plan plan = rv_editor_app_change_for(app, file);
        const char *why = nullptr;
        if (!app.session.live() || !rv_editor_app_can_reload(app)) {
            why = rv_editor_text("pane_assets.no_console");
        } else if (plan.action != rv_editor_change_action::refresh_texture) {
            why = rv_editor_text("pane_assets.not_texture");
        } else {
            why = rv_editor_app_why_not_reload(app);
        }
        const char *refresh_text = rv_editor_text("pane_assets.refresh");
        rv_editor_flow(rv_editor_button_width(refresh_text));
        rv_editor_state state;
        state.disabled = why;
        if (rv_editor_button(refresh_text, theme, state)) {
            app.texture_bake.message.clear();
            rv_editor_app_texture_bake_start(app, plan.name, file);
        }
        if (why == nullptr) {
            ImGui::SetItemTooltip("%s", rv_editor_text("pane_assets.refresh_tooltip"));
        }
    }
    rv_editor_shelf_end();

    rv_editor_well_begin("##well", ImVec2(0, 0), theme, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    std::vector<const rv_editor_asset *> shown;
    for (const rv_editor_asset &a : rv_editor_assets.files) {
        if ((ui.folder == 0 || a.folder == folders[static_cast<size_t>(ui.folder)]) &&
            (ui.filter[0] == '\0' || a.rel.find(ui.filter) != std::string::npos)) {
            shown.push_back(&a);
        }
    }

    // The preview strip: beside the list when the tile is wide, below it when narrow.
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const float font = ImGui::GetFontSize();
    const bool side_by_side = avail.x > font * side_by_side_threshold_em;
    const float preview_w_wide =
        std::clamp(avail.x / preview_width_divisor, font * preview_width_min_em, font * preview_width_max_em);
    const float preview_w = std::max(1.0f, side_by_side ? preview_w_wide : avail.x);
    const float preview_h_narrow =
        std::clamp(avail.y * preview_narrow_height_ratio, font * preview_height_min_em,
            font * preview_height_max_em);
    const float preview_h = std::max(1.0f, side_by_side ? avail.y : preview_h_narrow);
    const float list_w = std::max(1.0f, side_by_side ? avail.x - preview_w - ImGui::GetStyle().ItemSpacing.x : avail.x);
    const float list_h = std::max(1.0f, side_by_side ? avail.y : avail.y - preview_h - ImGui::GetStyle().ItemSpacing.y);

    const rv_editor_asset *selected = nullptr;
    for (const rv_editor_asset *a : shown) {
        if (a->rel == rv_editor_assets.selected) {
            selected = a;
            break;
        }
    }
    const auto selected_entry = selected ? rv_editor_assets.map.find(selected->rel) : rv_editor_assets.map.end();
    const rv_editor_map_entry *entry_ptr = selected_entry != rv_editor_assets.map.end() ? &selected_entry->second : nullptr;
    const rv_editor_icon selected_picture = selected ? rv_editor_asset_picture(renderer, *selected) : rv_editor_icon{};

    // The sound's duration: read once per selection, refreshed only if the file changed since.
    double selected_seconds = 0.0;
    if (selected != nullptr) {
        std::error_code ec;
        const auto write_time = std::filesystem::last_write_time(selected->path, ec);
        if (rv_editor_assets.sound_facts_for != selected->rel || rv_editor_assets.sound_facts_time != write_time) {
            rv_editor_assets.sound_facts_for = selected->rel;
            rv_editor_assets.sound_facts_time = write_time;
            rv_editor_assets.sound_seconds = rv_editor_asset_sound_seconds(*selected);
        }
        selected_seconds = rv_editor_assets.sound_seconds;
    }

    ImGui::BeginChild("##asset_list", ImVec2(list_w, list_h), false);
    if (shown.empty()) {
        const char *empty_msg;
        if (rv_editor_assets.files.empty()) {
            empty_msg = rv_editor_text("pane_assets.no_resources");
        } else {
            empty_msg = rv_editor_text("pane_assets.no_match_filter");
        }
        ImGui::TextDisabled("%s", empty_msg);
    } else if (ui.details) {
        if (ImGui::BeginTable("##assets", assets_table_columns,
                ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY)) {
            ImGui::TableSetupColumn(rv_editor_text("pane_assets.table_file"));
            ImGui::TableSetupColumn(rv_editor_text("pane_assets.table_kind"), ImGuiTableColumnFlags_WidthFixed);
            ImGui::TableSetupColumn(rv_editor_text("pane_assets.table_on_disc"));
            ImGui::TableSetupColumn(rv_editor_text("pane_assets.table_size"), ImGuiTableColumnFlags_WidthFixed);
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableHeadersRow();
            for (const rv_editor_asset *a : shown) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::Selectable(a->rel.c_str(), a->rel == rv_editor_assets.selected, ImGuiSelectableFlags_SpanAllColumns);
                rv_editor_asset_item(app, *a);
                const auto entry = rv_editor_assets.map.find(a->rel);
                ImGui::TableNextColumn();
                const char *kind_text =
                    entry == rv_editor_assets.map.end() ? entry_not_on_disc.data() : entry->second.kind.c_str();
                ImGui::TextUnformatted(kind_text);
                ImGui::TableNextColumn();
                const char *disc_name = entry == rv_editor_assets.map.end() ?
                    rv_editor_text("pane_assets.not_on_disc") :
                    entry->second.name.c_str();
                ImGui::TextUnformatted(disc_name);
                ImGui::TableNextColumn();
                ImGui::Text("%ju", a->size);
            }
            ImGui::EndTable();
        }
    } else {
        // Icons: a cell each, the picture or the kind's icon over a short label.
        const float cell = ImGui::GetFontSize() * icon_cell_size_em;
        const float art = ImGui::GetFontSize() * icon_glyph_size_em;
        const float width = ImGui::GetContentRegionAvail().x;
        const int columns = std::max(1, static_cast<int>(width / cell));
        ImDrawList *dl = ImGui::GetWindowDrawList();
        for (size_t i = 0; i < shown.size(); ++i) {
            const rv_editor_asset &a = *shown[i];
            if (i % static_cast<size_t>(columns) != 0) {
                ImGui::SameLine();
            }
            ImGui::PushID(a.rel.c_str());
            const ImVec2 p0 = ImGui::GetCursorScreenPos();
            ImGui::InvisibleButton(
                "##cell", ImVec2(cell, art + ImGui::GetTextLineHeightWithSpacing() + icon_cell_padding_px));
            rv_editor_asset_item(app, a);
            if (a.rel == rv_editor_assets.selected) {
                dl->AddRectFilled(p0, ImGui::GetItemRectMax(),
                    ImGui::GetColorU32(ImGuiCol_Header, selection_highlight_alpha));
            }
            if (ImGui::IsItemHovered()) {
                dl->AddRect(p0, ImGui::GetItemRectMax(), rv_editor_col(theme.selection));
            }
            const ImVec2 at(p0.x + (cell - art) * center_offset_ratio, p0.y + icon_art_y_offset_px);
            const rv_editor_icon picture = rv_editor_asset_picture(renderer, a);
            if (picture.id != ImTextureID{}) {
                const float k = std::min(art / static_cast<float>(picture.w), art / static_cast<float>(picture.h));
                const ImVec2 size(picture.w * k, picture.h * k);
                const ImVec2 q0(at.x + (art - size.x) * center_offset_ratio,
                    at.y + (art - size.y) * center_offset_ratio);
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
            // The label cut to the cell with an ellipsis; the whole name is in the tooltip.
            std::string label = a.path.filename().string();
            while (label.size() > label_ellipsis_min_chars &&
                ImGui::CalcTextSize(label.c_str()).x > cell - icon_label_margin_px) {
                label.erase(label.size() - label_ellipsis_erase_count);
                label += label_ellipsis;
            }
            const float lw = ImGui::CalcTextSize(label.c_str()).x;
            dl->AddText(ImVec2(p0.x + (cell - lw) * center_offset_ratio, at.y + art + icon_label_y_offset_px),
                rv_editor_col(theme.text), label.c_str());
            ImGui::PopID();
        }
    }
    ImGui::EndChild();

    if (side_by_side) {
        ImGui::SameLine();
    }
    // An always-present scrollbar: wrapped text otherwise changes the width, the height and the scrollbar each frame.
    ImGui::BeginChild("##asset_preview", ImVec2(preview_w, preview_h), true, ImGuiWindowFlags_AlwaysVerticalScrollbar);
    rv_editor_asset_preview(selected, entry_ptr, selected_picture, selected_seconds, theme, app.project);
    ImGui::EndChild();
    rv_editor_well_end();
}

} // namespace rv_editor
