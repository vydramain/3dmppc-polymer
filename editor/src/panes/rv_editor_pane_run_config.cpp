// Run Configuration: the project's run profiles (CFG-01), a list beside a form, in a
// window of its own. Nothing here touches a running session: Apply is for the next
// Run, Apply and Restart says so.

#include "panes/rv_editor_panes.hpp"

#include <algorithm>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

#include "imgui.h"

#include "theme/rv_editor_theme_imgui.hpp"
#include "ui/rv_editor_widgets.hpp"

namespace rv_editor
{

namespace
{

std::string rv_editor_run_lines(const std::vector<std::string> &items)
{
    std::string out;
    for (const std::string &item : items) {
        out += item + "\n";
    }
    return out;
}

std::vector<std::string> rv_editor_run_split(std::string_view text)
{
    std::vector<std::string> out;
    while (!text.empty()) {
        const size_t nl = text.find('\n');
        std::string_view line = text.substr(0, nl);
        text = nl == std::string_view::npos ? std::string_view() : text.substr(nl + 1);
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) {
            line.remove_suffix(1);
        }
        if (!line.empty()) {
            out.emplace_back(line);
        }
    }
    return out;
}

void rv_editor_run_form_fill(rv_editor_run_form &f, const rv_editor_run_profile &p)
{
    std::snprintf(f.name, sizeof(f.name), "%s", p.name.c_str());
    std::snprintf(f.runtime, sizeof(f.runtime), "%s", p.runtime.c_str());
    std::snprintf(f.memcard, sizeof(f.memcard), "%s", p.memcard.c_str());
    std::snprintf(f.cwd, sizeof(f.cwd), "%s", p.cwd.c_str());
    f.mute = p.mute;
    f.paused = p.paused;
    f.fixed_step = p.fixed_step;
    f.reload_on_save = p.reload_on_save;
    std::snprintf(f.args, sizeof(f.args), "%s", rv_editor_run_lines(p.args).c_str());
    std::snprintf(f.env, sizeof(f.env), "%s", rv_editor_run_lines(p.env).c_str());
}

rv_editor_run_profile rv_editor_run_form_profile(const rv_editor_run_form &f)
{
    return { f.name, f.runtime, f.memcard, f.cwd, f.mute, f.paused, f.fixed_step, rv_editor_run_split(f.args),
        rv_editor_run_split(f.env), f.reload_on_save };
}

// A label column, then the field with what an empty one means as its tooltip.
void rv_editor_run_path(const char *label, char *buf, size_t size, const char *empty_means, const rv_editor_theme &theme)
{
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    ImGui::SameLine(ImGui::GetFontSize() * 9.0f);
    ImGui::SetNextItemWidth(-1.0f);
    rv_editor_text_field((std::string("##") + label).c_str(), buf, size, theme);
    ImGui::SetItemTooltip("Empty: %s. Relative: to the project root.", empty_means);
}

// The profiles as a list on the left, with New and Delete under it; choosing one
// is itself applied: Run uses the one shown.
void rv_editor_run_profiles(rv_editor_app &app, float width, float height, const rv_editor_theme &theme)
{
    rv_editor_run_config &config = app.run_config;
    ImGui::BeginGroup();
    const float side = ImGui::GetFrameHeight();
    rv_editor_scroll_begin("##profiles", ImVec2(width, height - side - ImGui::GetStyle().ItemSpacing.y));
    for (size_t i = 0; i < config.profiles.size(); ++i) {
        ImGui::PushID(static_cast<int>(i));
        if (ImGui::Selectable(config.profiles[i].name.c_str(), i == config.active) && i != config.active) {
            config.active = i;
            rv_editor_app_profiles_save(app);
        }
        ImGui::PopID();
    }
    rv_editor_scroll_end(theme);
    if (rv_editor_letter_button("##new", 'N', theme.code_green, "New profile: a copy of this one, as last applied",
            theme)) {
        rv_editor_run_profile copy = config.profiles[config.active];
        copy.name = "Profile" + std::to_string(config.profiles.size() + 1);
        config.profiles.push_back(std::move(copy));
        config.active = config.profiles.size() - 1;
        rv_editor_app_profiles_save(app);
    }
    ImGui::SameLine();
    if (rv_editor_letter_button("##delete", 'X', theme.code_red, "Delete this profile", theme,
            { rv_editor_look::live, config.profiles.size() == 1 ? "The last profile stays" : nullptr })) {
        config.profiles.erase(config.profiles.begin() + static_cast<std::ptrdiff_t>(config.active));
        config.active = 0;
        rv_editor_app_profiles_save(app);
    }
    ImGui::EndGroup();
}

// The shown profile's fields, as edited until Apply.
void rv_editor_run_fields(rv_editor_app &app, ImVec2 size, const rv_editor_theme &theme)
{
    rv_editor_run_form &form = app.run_form;
    rv_editor_scroll_begin("##fields", size);
    if (!app.run_config.error.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, rv_editor_col(theme.error));
        ImGui::TextWrapped("%s: the defaults below are used; Apply writes the file anew.", app.run_config.error.c_str());
        ImGui::PopStyleColor();
    }
    if (app.session.live()) {
        ImGui::TextWrapped("Session %u keeps what it started with: Apply is for the next Run.", app.session.number());
    }
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Name");
    ImGui::SameLine(ImGui::GetFontSize() * 9.0f);
    ImGui::SetNextItemWidth(-1.0f);
    rv_editor_text_field("##name", form.name, sizeof(form.name), theme);
    rv_editor_run_path("Runtime", form.runtime, sizeof(form.runtime), "the runtime in File > Settings", theme);
    rv_editor_run_path("Memory card", form.memcard, sizeof(form.memcard), "the project's own card", theme);
    rv_editor_run_path("Working dir", form.cwd, sizeof(form.cwd), "the project root", theme);
    rv_editor_checkbox("Mute", &form.mute, theme);
    ImGui::SameLine(ImGui::GetFontSize() * 18.0f);
    rv_editor_checkbox("Start Paused", &form.paused, theme);
    ImGui::SetItemTooltip("Stopped before frame 0: Run, then Resume or Step");
    rv_editor_checkbox("Fixed Step", &form.fixed_step, theme);
    ImGui::SetItemTooltip("No real-time wait and no audio: every frame is 1/60 s of machine time");
    ImGui::SameLine(ImGui::GetFontSize() * 18.0f);
    rv_editor_checkbox("Reload On Save", &form.reload_on_save, theme);
    ImGui::SetItemTooltip("A saved .lua file reloads the entry script of a running session that can reload");
    const ImVec2 box(-1.0f, ImGui::GetTextLineHeight() * 3.5f);
    ImGui::TextUnformatted("More console options, one per line, before the disc");
    ImGui::InputTextMultiline("##args", form.args, sizeof(form.args), box);
    ImGui::TextUnformatted("Environment, KEY=VALUE per line, over the editor's own");
    ImGui::InputTextMultiline("##env", form.env, sizeof(form.env), box);
    rv_editor_scroll_end(theme);
}

// What is wrong with the edited profile, then Apply, Apply and Restart and Revert.
// True when the form was applied.
bool rv_editor_run_actions(rv_editor_app &app, const rv_editor_theme &theme)
{
    rv_editor_run_config &config = app.run_config;
    rv_editor_run_form &form = app.run_form;
    const rv_editor_run_profile edited = rv_editor_run_form_profile(form);
    std::string problem = rv_editor_run_profile_problem(edited, app.project.root);
    for (size_t i = 0; i < config.profiles.size() && problem.empty(); ++i) {
        if (i != config.active && config.profiles[i].name == edited.name) {
            problem = "Another profile is named " + edited.name;
        }
    }
    if (!problem.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, rv_editor_col(theme.error));
        ImGui::TextWrapped("%s", problem.c_str());
        ImGui::PopStyleColor();
    }
    const char *why_not = problem.empty() ? nullptr : "Fix what is said above first";
    const bool apply = rv_editor_button("Apply", theme, { rv_editor_look::live, why_not });
    ImGui::SetItemTooltip("For the next Run; a running session keeps what it started with");
    ImGui::SameLine();
    const bool restart = rv_editor_button("Apply and Restart", theme,
        { rv_editor_look::live, why_not != nullptr ? why_not : app.session.live() ? nullptr : "No session is running" });
    ImGui::SetItemTooltip("Stops the running session, then Run starts it again with this profile");
    ImGui::SameLine();
    if (rv_editor_button("Revert", theme)) {
        rv_editor_run_form_fill(form, config.profiles[config.active]);
    }
    if (apply || restart) {
        config.profiles[config.active] = edited;
        rv_editor_app_profiles_save(app);
    }
    if (restart) {
        app.run_after_stop = true;
        rv_editor_app_stop(app);
    }
    return apply || restart;
}

// The list beside the fields in `size`, the actions under both.
void rv_editor_run_editor(rv_editor_app &app, ImVec2 size, const rv_editor_theme &theme)
{
    rv_editor_run_config &config = app.run_config;
    if (app.run_form.loaded != app.run_config_revision) {
        rv_editor_run_form_fill(app.run_form, config.profiles[config.active]);
        app.run_form.loaded = app.run_config_revision;
    }
    const float list = ImGui::GetFontSize() * 12.0f;
    rv_editor_run_profiles(app, list, size.y, theme);
    ImGui::SameLine();
    rv_editor_run_fields(app, ImVec2(std::max(1.0f, size.x - list - ImGui::GetStyle().ItemSpacing.x), size.y), theme);
}

} // namespace

void rv_editor_pane_run_config(rv_editor_app &app, const rv_editor_theme &theme)
{
    if (!app.project.open) {
        rv_editor_open_project_row(app, theme);
        return;
    }
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const float actions = ImGui::GetFrameHeightWithSpacing() * 2.0f;
    rv_editor_run_editor(app, ImVec2(avail.x, std::max(ImGui::GetFrameHeight() * 6.0f, avail.y - actions)), theme);
    rv_editor_run_actions(app, theme);
}

} // namespace rv_editor
