// Assets: the project's resource files as an IRIX Icon Catalog (SCL-03): a
// picture or a kind icon with a short label, or a list with details; a folder
// and a filter. What each file is called on the disc comes from the burner's map
// of the last build (ADR 0011), never from a rule of the editor's; a file with a
// disc name can be dragged into a scene object's Mesh or Texture.

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
#include "theme/rv_editor_theme_imgui.hpp"
#include "ui/rv_editor_icons.hpp"
#include "ui/rv_editor_widgets.hpp"

namespace rv_editor
{

namespace
{

struct rv_editor_asset
{
    std::filesystem::path path;
    std::string rel;    // relative to the project root, as the map names it
    std::string folder; // its top-level folder
    uintmax_t size = 0;
};

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

// Its picture when it is a PNG the editor can read, else its kind's icon.
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

// The disc name, or why there is none yet; a drag of it when there is one.
void rv_editor_asset_item(rv_editor_app &app, const rv_editor_asset &a)
{
    const auto entry = rv_editor_assets.map.find(a.rel);
    if (entry == rv_editor_assets.map.end()) {
        ImGui::SetItemTooltip("%s\nNot on the disc of the last build: %s", a.rel.c_str(),
            rv_editor_assets.map.empty() ? "Build once, and the burner names each file"
                                         : "the manifest does not take it");
    } else {
        ImGui::SetItemTooltip("%s\nOn the disc: %s (%s%s%s)\nDrag it to a scene object's Mesh or Texture", a.rel.c_str(),
            entry->second.name.c_str(), entry->second.kind.c_str(), entry->second.parameter.empty() ? "" : ", ",
            entry->second.parameter.c_str());
        if (ImGui::BeginDragDropSource()) {
            ImGui::SetDragDropPayload("RV_ASSET", entry->second.name.c_str(), entry->second.name.size() + 1);
            ImGui::Text("%s", entry->second.name.c_str());
            ImGui::EndDragDropSource();
        }
    }
    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && a.path.extension() != ".png") {
        app.open_requests.push_back({ a.path, 0 });
    }
}

} // namespace

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
    if (shown.empty()) {
        ImGui::TextDisabled(rv_editor_assets.files.empty() ? "No resource files: put them in a folder such as assets/."
                                                           : "Nothing matches the folder and the filter.");
        return;
    }
    if (ui.details) {
        if (!ImGui::BeginTable("##assets", 4,
                ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY)) {
            return;
        }
        ImGui::TableSetupColumn("File");
        ImGui::TableSetupColumn("Kind", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableSetupColumn("On the disc");
        ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableHeadersRow();
        for (const rv_editor_asset *a : shown) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::Selectable(a->rel.c_str(), false, ImGuiSelectableFlags_SpanAllColumns);
            rv_editor_asset_item(app, *a);
            const auto entry = rv_editor_assets.map.find(a->rel);
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(entry == rv_editor_assets.map.end() ? "-" : entry->second.kind.c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(entry == rv_editor_assets.map.end() ? "not on the disc" : entry->second.name.c_str());
            ImGui::TableNextColumn();
            ImGui::Text("%ju", a->size);
        }
        ImGui::EndTable();
        return;
    }
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
            // The file's letter at twice the font: whole pixels of the 5x7 cell.
            char letter = 'F';
            uint32_t color = 0;
            rv_editor_file_chip(a.path, letter, color);
            const char text[2] = { letter, '\0' };
            const float size = ImGui::GetFontSize() * 2.0f;
            const float w = ImGui::GetFont()->CalcTextSizeA(size, FLT_MAX, 0.0f, text).x;
            dl->AddText(ImGui::GetFont(), size, ImVec2(std::floor(at.x + (art - w) * 0.5f),
                std::floor(at.y + (art - size) * 0.5f)), rv_editor_col(color), text);
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

} // namespace rv_editor
