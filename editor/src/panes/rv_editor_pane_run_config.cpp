// Run Configuration: the project's run profiles (CFG-01) as a form. Nothing here
// touches a running session: Apply is for the next Run, Apply and Restart says so.

#include "panes/rv_editor_panes.hpp"

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
    std::snprintf(f.args, sizeof(f.args), "%s", rv_editor_run_lines(p.args).c_str());
    std::snprintf(f.env, sizeof(f.env), "%s", rv_editor_run_lines(p.env).c_str());
}

rv_editor_run_profile rv_editor_run_form_profile(const rv_editor_run_form &f)
{
    return { f.name, f.runtime, f.memcard, f.cwd, f.mute, f.paused, f.fixed_step, rv_editor_run_split(f.args),
        rv_editor_run_split(f.env) };
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

} // namespace

void rv_editor_pane_run_config(rv_editor_app &app, const rv_editor_theme &theme)
{
    if (!app.project.open) {
        rv_editor_open_project_row(app, theme);
        return;
    }
    rv_editor_run_config &config = app.run_config;
    rv_editor_run_form &form = app.run_form;
    if (form.loaded != app.run_config_revision) {
        rv_editor_run_form_fill(form, config.profiles[config.active]);
        form.loaded = app.run_config_revision;
    }
    if (!config.error.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, rv_editor_col(theme.error));
        ImGui::TextWrapped("%s: the defaults below are used; Apply writes the file anew.", config.error.c_str());
        ImGui::PopStyleColor();
    }

    // Choosing a profile is itself applied: Run uses the one shown.
    std::vector<const char *> names;
    for (const rv_editor_run_profile &p : config.profiles) {
        names.push_back(p.name.c_str());
    }
    int current = static_cast<int>(config.active);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Profile");
    ImGui::SameLine(ImGui::GetFontSize() * 9.0f);
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 12.0f);
    if (rv_editor_dropdown("##profile", &current, names.data(), static_cast<int>(names.size()), theme)) {
        config.active = static_cast<size_t>(current);
        rv_editor_app_profiles_save(app);
    }
    ImGui::SameLine();
    if (rv_editor_button("New", theme)) {
        rv_editor_run_profile copy = config.profiles[config.active];
        copy.name = "Profile" + std::to_string(config.profiles.size() + 1);
        config.profiles.push_back(std::move(copy));
        config.active = config.profiles.size() - 1;
        rv_editor_app_profiles_save(app);
    }
    ImGui::SetItemTooltip("A copy of this profile, as last applied");
    ImGui::SameLine();
    if (rv_editor_button("Delete", theme,
            { rv_editor_look::live, config.profiles.size() == 1 ? "The last profile stays" : nullptr })) {
        config.profiles.erase(config.profiles.begin() + static_cast<std::ptrdiff_t>(config.active));
        config.active = 0;
        rv_editor_app_profiles_save(app);
    }
    if (app.session.live()) {
        ImGui::TextWrapped("Session %u keeps what it started with: Apply is for the next Run.", app.session.number());
    }

    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Name");
    ImGui::SameLine(ImGui::GetFontSize() * 9.0f);
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 12.0f);
    rv_editor_text_field("##name", form.name, sizeof(form.name), theme);
    rv_editor_run_path("Runtime", form.runtime, sizeof(form.runtime), "the runtime in File > Settings", theme);
    rv_editor_run_path("Memory card", form.memcard, sizeof(form.memcard), "the project's own card", theme);
    rv_editor_run_path("Working dir", form.cwd, sizeof(form.cwd), "the project root", theme);
    rv_editor_checkbox("Mute", &form.mute, theme);
    ImGui::SameLine();
    rv_editor_checkbox("Start Paused", &form.paused, theme);
    ImGui::SetItemTooltip("Stopped before frame 0: Run, then Resume or Step");
    ImGui::SameLine();
    rv_editor_checkbox("Fixed Step", &form.fixed_step, theme);
    ImGui::SetItemTooltip("No real-time wait and no audio: every frame is 1/60 s of machine time");
    const ImVec2 box(-1.0f, ImGui::GetTextLineHeight() * 3.5f);
    ImGui::TextUnformatted("More console options, one per line, before the disc");
    ImGui::InputTextMultiline("##args", form.args, sizeof(form.args), box);
    ImGui::TextUnformatted("Environment, KEY=VALUE per line, over the editor's own");
    ImGui::InputTextMultiline("##env", form.env, sizeof(form.env), box);

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
}

} // namespace rv_editor
