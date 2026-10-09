// Run Configuration: the project's run profiles, a list beside a form, in a
// window of its own. Nothing here touches a running session: Apply is for the next
// Run, Apply and Restart says so.

#include "panes/rv_editor_panes.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

#include "imgui.h"

#include "text/rv_editor_text.hpp"
#include "theme/rv_editor_theme_imgui.hpp"
#include "ui/rv_editor_glyphs.hpp"
#include "ui/rv_editor_widgets.hpp"

namespace rv_editor
{

namespace
{

// Label column position in em units.
constexpr float label_column_em = 9.0f;
// Checkbox alignment at double the label column in em units.
constexpr float checkbox_spacing_em = 18.0f;
// Multiline input box height in text lines.
constexpr float multiline_box_lines = 3.5f;
// Profile list width in em units.
constexpr float profile_list_width_em = 12.0f;
// Actions area height in frame heights with spacing.
constexpr float actions_height_frames = 2.0f;
// Minimum well content area height in frame heights.
constexpr float min_well_height_frames = 6.0f;

// The Run Configuration form: the active profile as edited until Apply. Refilled when
// run_config_revision moves, which a profile change in the app does.
struct rv_editor_run_form {
    uint64_t loaded = 0; // the run_config_revision it was filled at
    char name[short_text_field_size] = {};
    char runtime[filesystem_path_field_size] = {};
    char memcard[filesystem_path_field_size] = {};
    char cwd[filesystem_path_field_size] = {};
    bool mute = false;
    bool paused = false;
    bool fixed_step = false;
    bool reload_on_save = false;
    char args[command_line_field_size] = {};
    char env[command_line_field_size] = {};
};

rv_editor_run_form run_form_draft;

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
    return { f.name,
        f.runtime,
        f.memcard,
        f.cwd,
        f.mute,
        f.paused,
        f.fixed_step,
        rv_editor_run_split(f.args),
        rv_editor_run_split(f.env),
        f.reload_on_save };
}

// A group heading, in the console's own slot order: session-level fields first,
// then one group per rv_pcslots slot that this page has a field for.
void rv_editor_run_group(const char *display_label, const rv_editor_theme &theme)
{
    ImGui::PushStyleColor(ImGuiCol_Text, rv_editor_col(theme.text_bright));
    ImGui::TextUnformatted(display_label);
    ImGui::PopStyleColor();
    ImGui::Separator();
}

// A label column, then the field with what an empty one means as its tooltip.
void rv_editor_run_path(const char *label,
    char *buf,
    size_t size,
    const char *empty_means_key,
    const rv_editor_theme &theme,
    const char *display_label)
{
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(display_label);
    ImGui::SameLine(ImGui::GetFontSize() * label_column_em);
    ImGui::SetNextItemWidth(-1.0f);
    rv_editor_text_field((std::string("##") + label).c_str(), buf, size, theme);
    const auto empty_means = rv_editor_text(empty_means_key);
    const auto tip = rv_editor_text_format("pane_run_config.empty_means", std::make_format_args(empty_means));
    ImGui::SetItemTooltip("%s", tip.c_str());
}

// Any profile request waits: the list and the profile buttons wait with it.
bool rv_editor_run_profile_pending(const rv_editor_app &app)
{
    constexpr std::array profile_kinds = { rv_editor_pane_request_kind::profile_select,
        rv_editor_pane_request_kind::profile_add,
        rv_editor_pane_request_kind::profile_delete,
        rv_editor_pane_request_kind::profile_apply,
        rv_editor_pane_request_kind::profile_apply_restart };
    return std::ranges::any_of(profile_kinds, [&app](rv_editor_pane_request_kind kind) {
        return rv_editor_app_request_pending(app, kind);
    });
}

// A request this pane asks of the app; the choice stays unset.
rv_editor_pane_request rv_editor_run_request(rv_editor_pane_request_kind kind, const rv_editor_run_profile &profile = {})
{
    rv_editor_pane_request request;
    request.kind = kind;
    request.profile = profile;
    return request;
}

// The profiles as a list on the left, with New and Delete under it; choosing one
// is itself applied: Run uses the one shown. The app makes each change.
void rv_editor_run_profiles(rv_editor_app &app, float width, float height, const rv_editor_theme &theme)
{
    const rv_editor_run_config &config = app.run_config;
    const bool pending = rv_editor_run_profile_pending(app);
    const char *why = pending ? rv_editor_text("app_why_not.request_pending") : nullptr;
    ImGui::BeginGroup();
    const float side = ImGui::GetFrameHeight();
    rv_editor_scroll_begin("##profiles", ImVec2(width, height - side - ImGui::GetStyle().ItemSpacing.y));
    for (size_t i = 0; i < config.profiles.size(); ++i) {
        ImGui::PushID(static_cast<int>(i));
        if (ImGui::Selectable(config.profiles[i].name.c_str(), i == config.active) && i != config.active && !pending) {
            rv_editor_pane_request select = rv_editor_run_request(rv_editor_pane_request_kind::profile_select);
            select.choice = i;
            (void)rv_editor_app_request(app, select);
        }
        if (pending) {
            ImGui::SetItemTooltip("%s", why);
        }
        ImGui::PopID();
    }
    rv_editor_scroll_end(theme);
    if (rv_editor_letter_button("##new",
            rv_editor_glyph::new_,
            theme.code_green,
            rv_editor_text("pane_run_config.new_profile_tooltip"),
            theme,
            { rv_editor_look::live, why })) {
        (void)rv_editor_app_request(app, rv_editor_run_request(rv_editor_pane_request_kind::profile_add));
    }
    ImGui::SameLine();
    const char *delete_why = why;
    if (delete_why == nullptr && config.profiles.size() == 1) {
        delete_why = rv_editor_text("pane_run_config.delete_last_profile");
    }
    if (rv_editor_letter_button("##delete",
            rv_editor_glyph::delete_,
            theme.code_red,
            rv_editor_text("pane_run_config.delete_profile_tooltip"),
            theme,
            { rv_editor_look::live, delete_why })) {
        (void)rv_editor_app_request(app, rv_editor_run_request(rv_editor_pane_request_kind::profile_delete));
    }
    ImGui::EndGroup();
}

// The shown profile's fields, as edited until Apply.
void rv_editor_run_fields(rv_editor_app &app, ImVec2 size, const rv_editor_theme &theme)
{
    rv_editor_run_form &form = run_form_draft;
    rv_editor_scroll_begin("##fields", size);
    if (!app.run_config.error.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, rv_editor_col(theme.error));
        const auto err = rv_editor_text_format("pane_run_config.profile_error", std::make_format_args(app.run_config.error));
        ImGui::TextWrapped("%s", err.c_str());
        ImGui::PopStyleColor();
    }
    if (app.session.live()) {
        const auto session_num = app.session.number();
        const auto msg = rv_editor_text_format("pane_run_config.session_active", std::make_format_args(session_num));
        ImGui::TextWrapped("%s", msg.c_str());
    }
    rv_editor_run_group(rv_editor_text("pane_run_config.session_group"), theme);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(rv_editor_text("pane_run_config.name_label"));
    ImGui::SameLine(ImGui::GetFontSize() * label_column_em);
    ImGui::SetNextItemWidth(-1.0f);
    rv_editor_text_field("##name", form.name, sizeof(form.name), theme);
    rv_editor_run_path("Runtime",
        form.runtime,
        sizeof(form.runtime),
        "pane_run_config.runtime_empty_tooltip",
        theme,
        rv_editor_text("pane_run_config.runtime_label"));
    rv_editor_run_path("Working dir",
        form.cwd,
        sizeof(form.cwd),
        "pane_run_config.working_dir_empty_tooltip",
        theme,
        rv_editor_text("pane_run_config.working_dir_label"));
    rv_editor_checkbox(rv_editor_text("pane_run_config.start_paused_label"), &form.paused, theme);
    ImGui::SetItemTooltip("%s", rv_editor_text("pane_run_config.start_paused_tooltip"));
    ImGui::SameLine(ImGui::GetFontSize() * checkbox_spacing_em);
    rv_editor_checkbox(rv_editor_text("pane_run_config.fixed_step_label"), &form.fixed_step, theme);
    ImGui::SetItemTooltip("%s", rv_editor_text("pane_run_config.fixed_step_tooltip"));
    rv_editor_checkbox(rv_editor_text("pane_run_config.reload_on_save_label"), &form.reload_on_save, theme);
    ImGui::SetItemTooltip("%s", rv_editor_text("pane_run_config.reload_on_save_tooltip"));
    const ImVec2 box(-1.0f, ImGui::GetTextLineHeight() * multiline_box_lines);
    ImGui::TextUnformatted(rv_editor_text("pane_run_config.console_options_label"));
    ImGui::InputTextMultiline("##args", form.args, sizeof(form.args), box);
    ImGui::TextUnformatted(rv_editor_text("pane_run_config.environment_label"));
    ImGui::InputTextMultiline("##env", form.env, sizeof(form.env), box);

    ImGui::Spacing();
    rv_editor_run_group(rv_editor_text("pane_run_config.audio_group"), theme);
    rv_editor_checkbox(rv_editor_text("pane_run_config.mute_label"), &form.mute, theme);

    ImGui::Spacing();
    rv_editor_run_group(rv_editor_text("pane_run_config.memory_card_group"), theme);
    rv_editor_run_path("Memory card",
        form.memcard,
        sizeof(form.memcard),
        "pane_run_config.memory_card_empty_tooltip",
        theme,
        rv_editor_text("pane_run_config.memory_card_label"));
    rv_editor_scroll_end(theme);
}

// Why Apply and Restart waits, the first that holds: a restart already runs (it wins whatever
// the profile says), a profile request waits, the profile has a problem, no session runs.
const char *rv_editor_run_restart_why(const rv_editor_app &app, bool pending, const char *edit_why)
{
    if (rv_editor_app_request_pending(app, rv_editor_pane_request_kind::restart)) {
        return rv_editor_text("app_why_not.restart_pending");
    }
    if (pending) {
        return rv_editor_text("app_why_not.request_pending");
    }
    if (edit_why != nullptr) {
        return edit_why;
    }
    return app.session.live() ? nullptr : rv_editor_text("pane_run_config.restart_disabled");
}

// The problem of the edited profile, then Apply, Apply and Restart and Revert.
void rv_editor_run_actions(rv_editor_app &app, const rv_editor_theme &theme)
{
    const rv_editor_run_config &config = app.run_config;
    const rv_editor_run_profile edited = rv_editor_run_form_profile(run_form_draft);
    const std::string problem = rv_editor_app_profile_problem(app, edited);
    if (!problem.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, rv_editor_col(theme.error));
        ImGui::TextWrapped("%s", problem.c_str());
        ImGui::PopStyleColor();
    }
    const bool pending = rv_editor_run_profile_pending(app);
    const char *edit_why = problem.empty() ? nullptr : rv_editor_text("pane_run_config.apply_disabled");
    const char *apply_why = pending ? rv_editor_text("app_why_not.request_pending") : edit_why;
    const bool apply =
        rv_editor_button(rv_editor_text("pane_run_config.apply_label"), theme, { rv_editor_look::live, apply_why });
    ImGui::SetItemTooltip("%s", rv_editor_text("pane_run_config.apply_button_tooltip"));
    ImGui::SameLine();
    const char *restart_why = rv_editor_run_restart_why(app, pending, edit_why);
    const bool restart =
        rv_editor_button(rv_editor_text("pane_run_config.apply_restart_label"), theme, { rv_editor_look::live, restart_why });
    ImGui::SetItemTooltip("%s", rv_editor_text("pane_run_config.apply_restart_tooltip"));
    ImGui::SameLine();
    if (rv_editor_button(rv_editor_text("pane_run_config.revert_label"), theme)) {
        rv_editor_run_form_fill(run_form_draft, config.profiles[config.active]);
    }
    if (apply) {
        (void)rv_editor_app_request(app, rv_editor_run_request(rv_editor_pane_request_kind::profile_apply, edited));
    }
    if (restart) {
        (void)rv_editor_app_request(app, rv_editor_run_request(rv_editor_pane_request_kind::profile_apply_restart, edited));
    }
}

// The list beside the fields in `size`, the actions under both.
void rv_editor_run_editor(rv_editor_app &app, ImVec2 size, const rv_editor_theme &theme)
{
    rv_editor_run_config &config = app.run_config;
    if (run_form_draft.loaded != app.run_config_revision) {
        rv_editor_run_form_fill(run_form_draft, config.profiles[config.active]);
        run_form_draft.loaded = app.run_config_revision;
    }
    const float list = ImGui::GetFontSize() * profile_list_width_em;
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
    const float actions = ImGui::GetFrameHeightWithSpacing() * actions_height_frames;
    // The two-column body in one well; its size still comes from the region left
    // once the well's own padding is taken out.
    const float well_height = std::max(ImGui::GetFrameHeight() * min_well_height_frames, avail.y - actions);
    rv_editor_well_begin("##well", ImVec2(0, well_height), theme);
    rv_editor_run_editor(app, ImGui::GetContentRegionAvail(), theme);
    rv_editor_well_end();
    rv_editor_run_actions(app, theme);
}

} // namespace rv_editor
