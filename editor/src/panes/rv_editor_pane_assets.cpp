// Assets: the project's resource files as an IRIX Icon Catalog (SCL-03): a
// picture or a kind icon with a short label, or a list with details; a folder
// and a filter. What each file is called on the disc comes from the burner's map
// of the last build (ADR 0011), never from a rule of the editor's.

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
            if (name.starts_with('.') || name.starts_with("build") || (it.depth() == 0 && name == "src")) {
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
    if (rv_editor_assets.root != app.project.root || now - rv_editor_assets.read > std::chrono::seconds(1)) {
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
        ImGui::SetItemTooltip("%s\nNot on the disc of the last build: %s", a.rel.c_str(),
            rv_editor_assets.map.empty() ? "Build once, and the burner names each file"
                                         : "the manifest does not take it");
    } else {
        ImGui::SetItemTooltip("%s\nOn the disc: %s (%s%s%s)", a.rel.c_str(), entry->second.name.c_str(),
            entry->second.kind.c_str(), entry->second.parameter.empty() ? "" : ", ", entry->second.parameter.c_str());
    }
    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        const std::string ext = a.path.extension().string();
        if (ext == ".png" || ext == ".wav" || ext == ".pcm") {
            rv_editor_app_scene_tab_open(app, a.path);
            app.layout_request = rv_editor_layout_preset::scene;
            app.show_request = rv_editor_pane_kind::scene;
        } else if (a.rel.ends_with(".scene.toml")) {
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
    std::vector<std::string> folders{ "All folders" };
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
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 9.0f);
    rv_editor_dropdown("##folder", &ui.folder, names.data(), static_cast<int>(names.size()), theme);
    rv_editor_flow(ImGui::GetFontSize() * 9.0f);
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 9.0f);
    rv_editor_text_field("##filter", ui.filter, sizeof(ui.filter), theme);
    ImGui::SetItemTooltip("Filter: only names holding this text");
    rv_editor_flow(rv_editor_checkbox_width("Details"));
    rv_editor_checkbox("Details", &ui.details, theme);

    std::vector<const rv_editor_asset *> shown;
    for (const rv_editor_asset &a : rv_editor_assets.files) {
        if ((ui.folder == 0 || a.folder == folders[static_cast<size_t>(ui.folder)]) &&
            (ui.filter[0] == '\0' || a.rel.find(ui.filter) != std::string::npos)) {
            shown.push_back(&a);
        }
    }

    // The preview strip: beside the list when the tile is wide, below it when narrow.
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const bool side_by_side = avail.x > ImGui::GetFontSize() * 28.0f;
    const float preview_w = std::max(1.0f, side_by_side
        ? std::clamp(avail.x / 3.0f, ImGui::GetFontSize() * 14.0f, ImGui::GetFontSize() * 32.0f)
        : avail.x);
    const float preview_h = std::max(1.0f, side_by_side
        ? avail.y
        : std::clamp(avail.y * 0.4f, ImGui::GetFontSize() * 6.0f, ImGui::GetFontSize() * 14.0f));
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
        ImGui::TextDisabled(rv_editor_assets.files.empty() ? "No resource files: put them in a folder such as assets/."
                                                           : "Nothing matches the folder and the filter.");
    } else if (ui.details) {
        if (ImGui::BeginTable("##assets", 4,
                ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY)) {
            ImGui::TableSetupColumn("File");
            ImGui::TableSetupColumn("Kind", ImGuiTableColumnFlags_WidthFixed);
            ImGui::TableSetupColumn("On the disc");
            ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthFixed);
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableHeadersRow();
            for (const rv_editor_asset *a : shown) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::Selectable(a->rel.c_str(), a->rel == rv_editor_assets.selected, ImGuiSelectableFlags_SpanAllColumns);
                rv_editor_asset_item(app, *a);
                const auto entry = rv_editor_assets.map.find(a->rel);
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(entry == rv_editor_assets.map.end() ? "-" : entry->second.kind.c_str());
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(
                    entry == rv_editor_assets.map.end() ? "not on the disc" : entry->second.name.c_str());
                ImGui::TableNextColumn();
                ImGui::Text("%ju", a->size);
            }
            ImGui::EndTable();
        }
    } else {
        // Icons: a cell each, the picture or the kind's icon over a short label.
        const float cell = ImGui::GetFontSize() * 5.5f;
        const float art = ImGui::GetFontSize() * 3.0f;
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
            ImGui::InvisibleButton("##cell", ImVec2(cell, art + ImGui::GetTextLineHeightWithSpacing() + 4.0f));
            rv_editor_asset_item(app, a);
            if (a.rel == rv_editor_assets.selected) {
                dl->AddRectFilled(p0, ImGui::GetItemRectMax(), ImGui::GetColorU32(ImGuiCol_Header, 0.35f));
            }
            if (ImGui::IsItemHovered()) {
                dl->AddRect(p0, ImGui::GetItemRectMax(), rv_editor_col(theme.selection));
            }
            const ImVec2 at(p0.x + (cell - art) * 0.5f, p0.y + 2.0f);
            const rv_editor_icon picture = rv_editor_asset_picture(renderer, a);
            if (picture.id != ImTextureID{}) {
                const float k = std::min(art / static_cast<float>(picture.w), art / static_cast<float>(picture.h));
                const ImVec2 size(picture.w * k, picture.h * k);
                const ImVec2 q0(at.x + (art - size.x) * 0.5f, at.y + (art - size.y) * 0.5f);
                dl->AddImage(picture.id, q0, ImVec2(q0.x + size.x, q0.y + size.y));
            } else {
                // The file's code, sized down from twice the font until it fits the tile's width.
                const char *code = rv_editor_glyph::other_file;
                uint32_t color = 0;
                rv_editor_file_chip(a.path, code, color);
                float size = ImGui::GetFontSize() * 2.0f;
                float w = ImGui::GetFont()->CalcTextSizeA(size, FLT_MAX, 0.0f, code).x;
                if (w > art * 0.9f) {
                    size *= art * 0.9f / w;
                    w = art * 0.9f;
                }
                dl->AddText(ImGui::GetFont(), size, ImVec2(std::floor(at.x + (art - w) * 0.5f),
                    std::floor(at.y + (art - size) * 0.5f)), rv_editor_col(color), code);
            }
            // The label cut to the cell with an ellipsis; the whole name is in the tooltip.
            std::string label = a.path.filename().string();
            while (label.size() > 3 && ImGui::CalcTextSize(label.c_str()).x > cell - 4.0f) {
                label.erase(label.size() - 4);
                label += "...";
            }
            const float lw = ImGui::CalcTextSize(label.c_str()).x;
            dl->AddText(ImVec2(p0.x + (cell - lw) * 0.5f, at.y + art + 2.0f), rv_editor_col(theme.text), label.c_str());
            ImGui::PopID();
        }
    }
    ImGui::EndChild();

    if (side_by_side) {
        ImGui::SameLine();
    }
    ImGui::BeginChild("##asset_preview", ImVec2(preview_w, preview_h), true);
    rv_editor_asset_preview(selected, entry_ptr, selected_picture, selected_seconds, theme, app.project);
    ImGui::EndChild();
}

} // namespace rv_editor
