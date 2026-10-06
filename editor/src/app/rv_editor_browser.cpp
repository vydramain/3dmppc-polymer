// The file browser pages draw in place of SDL's file dialogs: no window of its
// own, no other thread; the page around it stays usable.

#include "app/rv_editor_browser.hpp"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <system_error>

#include "imgui.h"

#include "text/rv_editor_text.hpp"
#include "theme/rv_editor_theme_imgui.hpp"
#include "ui/rv_editor_widgets.hpp"

namespace rv_editor
{

namespace
{

// Footer height in framed rows: path field and buttons.
constexpr float footer_frames = 2.0f;

// Smallest list height in frames.
constexpr float list_min_frames = 3.0f;

constexpr const char *env_home = "HOME";    // home directory the browser starts in
constexpr std::string_view root_path = "/"; // fallback start directory when HOME is unset
constexpr const char *dir_marker = "/";     // appended to directory names in the listing

void rv_editor_browser_go(rv_editor_browser &b, const std::filesystem::path &dir)
{
    b.dir = dir;
    b.selected.clear();
    const std::string text = dir.string();
    std::strncpy(b.path, text.c_str(), sizeof(b.path) - 1);
    b.path[sizeof(b.path) - 1] = '\0';
}

void rv_editor_browser_list(rv_editor_browser &b)
{
    if (b.listed == b.dir && !b.listed.empty()) {
        return;
    }
    b.listed = b.dir;
    b.entries.clear();
    b.error.clear();
    std::error_code ec;
    for (std::filesystem::directory_iterator it(b.dir, ec), end; !ec && it != end; it.increment(ec)) {
        const std::string name = it->path().filename().string();
        if (name.empty() || name[0] == '.') {
            continue;
        }
        std::error_code type_ec;
        const bool is_dir = it->is_directory(type_ec);
        if (is_dir || b.pick == rv_editor_browse_pick::executable) {
            b.entries.push_back({ name, is_dir });
        }
    }
    if (ec) {
        b.error = b.dir.string() + ": " + ec.message();
    }
    std::sort(b.entries.begin(), b.entries.end(), [](const auto &l, const auto &r) {
        return l.dir != r.dir ? l.dir : l.name < r.name;
    });
}

bool rv_editor_is_executable(const std::filesystem::path &p)
{
    std::error_code ec;
    const std::filesystem::file_status st = std::filesystem::status(p, ec);
    return !ec && std::filesystem::is_regular_file(st) &&
        (st.permissions() & std::filesystem::perms::owner_exec) != std::filesystem::perms::none;
}

} // namespace

void rv_editor_browser_start(rv_editor_browser &b,
    std::string purpose,
    std::string action,
    rv_editor_browse_pick pick,
    const std::filesystem::path &start)
{
    b = {};
    b.purpose = std::move(purpose);
    b.action = std::move(action);
    b.pick = pick;
    std::filesystem::path dir = start;
    if (dir.empty()) {
        const char *home = std::getenv(env_home);
        dir = home != nullptr ? home : root_path;
    }
    std::error_code ec;
    while (!dir.empty() && !std::filesystem::is_directory(dir, ec) && dir != dir.parent_path()) {
        dir = dir.parent_path();
    }
    rv_editor_browser_go(b, dir.empty() ? std::filesystem::path(root_path) : dir);
}

std::filesystem::path rv_editor_browser_target(const rv_editor_browser &b)
{
    if (!b.selected.empty()) {
        return b.selected;
    }
    return b.pick == rv_editor_browse_pick::directory ? b.dir : std::filesystem::path();
}

rv_editor_browse_result rv_editor_browser_draw(rv_editor_browser &b,
    float height,
    const char *why_not,
    std::filesystem::path &out,
    const rv_editor_theme &theme)
{
    rv_editor_browser_list(b);
    rv_editor_browse_result result = rv_editor_browse_result::none;
    ImGui::PushID(&b);
    ImGui::PushStyleColor(ImGuiCol_Text, rv_editor_col(theme.text_bright));
    ImGui::TextUnformatted(b.purpose.c_str());
    ImGui::PopStyleColor();

    const std::filesystem::path parent = b.dir.parent_path();
    if (rv_editor_button(rv_editor_text("browser.up"),
            theme,
            { rv_editor_look::live, parent == b.dir ? rv_editor_text("browser.up_tooltip") : nullptr })) {
        rv_editor_browser_go(b, parent);
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1.0f);
    rv_editor_field path_field;
    path_field.invalid = b.error.empty() ? nullptr : b.error.c_str();
    rv_editor_text_field("##path", b.path, sizeof(b.path), theme, path_field);
    // A typed path is followed once it names a directory.
    std::error_code ec;
    if (ImGui::IsItemDeactivatedAfterEdit() && std::filesystem::is_directory(b.path, ec)) {
        rv_editor_browser_go(b, b.path);
    }

    const float footer = ImGui::GetFrameHeightWithSpacing() * footer_frames;
    const float list_room = height - footer - ImGui::GetFrameHeightWithSpacing() * footer_frames;
    const float list_h = height > 0.0f ? std::max(list_room, ImGui::GetFrameHeight() * list_min_frames) : -footer;
    if (ImGui::BeginListBox("##entries", ImVec2(-1.0f, list_h))) {
        std::filesystem::path enter;
        for (const rv_editor_browser::rv_editor_entry &e : b.entries) {
            const std::filesystem::path full = b.dir / e.name;
            const std::string label = e.name + (e.dir ? dir_marker : "");
            const bool selected = b.selected == full;
            if (!e.dir) {
                ImGui::PushStyleColor(ImGuiCol_Text, rv_editor_col(theme.text_disabled));
            }
            if (ImGui::Selectable(label.c_str(), selected, ImGuiSelectableFlags_AllowDoubleClick)) {
                b.selected = full;
                if (ImGui::IsMouseDoubleClicked(0)) {
                    if (e.dir) {
                        enter = full;
                    } else if (why_not == nullptr && rv_editor_is_executable(full)) {
                        out = full;
                        result = rv_editor_browse_result::picked;
                    }
                }
            }
            if (!e.dir) {
                ImGui::PopStyleColor();
            }
        }
        ImGui::EndListBox();
        if (!enter.empty()) {
            rv_editor_browser_go(b, enter);
        }
    }

    const std::filesystem::path target = rv_editor_browser_target(b);
    const bool usable = b.pick == rv_editor_browse_pick::directory ? !target.empty() : rv_editor_is_executable(target);
    const char *why = why_not != nullptr            ? why_not :
        usable                                      ? nullptr :
        b.pick == rv_editor_browse_pick::executable ? rv_editor_text("browser.select_executable") :
                                                      rv_editor_text("browser.select_directory");
    ImGui::TextUnformatted(target.empty() ? rv_editor_text("browser.nothing_selected") : target.c_str());
    if (rv_editor_button(b.action.c_str(), theme, { rv_editor_look::live, why })) {
        out = target;
        result = rv_editor_browse_result::picked;
    }
    ImGui::SameLine();
    if (rv_editor_button(rv_editor_text("browser.cancel"), theme)) {
        result = rv_editor_browse_result::cancelled;
    }
    ImGui::PopID();
    return result;
}

} // namespace rv_editor
