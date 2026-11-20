// Run Configuration: the project's run profiles, a list beside a form, in a
// window of its own. Nothing here touches a running session: Apply is for the next
// Run, Apply and Restart says so.

#include "panes/rv_editor_panes.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

#include "imgui.h"

#include "panes/rv_editor_pane_tiled.hpp"
#include "text/rv_editor_text.hpp"
#include "ui/rv_editor_glyphs.hpp"
#include "ui/rv_editor_widgets.hpp"
#include "ui/rv_editor_widgets_content.hpp"
#include "ui/rv_editor_widgets_form.hpp"
#include "ui/rv_editor_widgets_message.hpp"

namespace rv_editor
{

namespace
{

// Multiline input box height in text lines.
constexpr float multiline_box_lines = 3.0f;
// Characters of the narrowest field of the form.
constexpr int min_field_chars = 16;

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

// The rows of the form: the label and what the field shows (a path from its root, or whole outside it), for the label column
// and the width.
struct rv_editor_run_rows {
    std::string runtime, cwd, memcard;
};

std::vector<rv_editor_form_row_desc> rv_editor_run_descs(const rv_editor_run_form &form, const rv_editor_run_rows &paths)
{
    static const std::string sample(min_field_chars, '0');
    return { { rv_editor_text("pane_run_config.name_label"), form.name, nullptr, false },
        { rv_editor_text("pane_run_config.runtime_label"), paths.runtime.c_str(), nullptr, true },
        { rv_editor_text("pane_run_config.working_dir_label"), paths.cwd.c_str(), nullptr, false },
        { rv_editor_text("pane_run_config.memory_card_label"), paths.memcard.c_str(), nullptr, false },
        { rv_editor_text("pane_run_config.start_paused_label"), "", nullptr, false },
        { rv_editor_text("pane_run_config.fixed_step_label"), "", nullptr, false },
        { rv_editor_text("pane_run_config.reload_on_save_label"), "", nullptr, false },
        { rv_editor_text("pane_run_config.mute_label"), "", nullptr, false },
        { rv_editor_text("pane_run_config.console_options_key"), sample.c_str(), nullptr, false },
        { rv_editor_text("pane_run_config.environment_key"), sample.c_str(), nullptr, false } };
}

// A path field in the form: the path from the project root, whole, with what an empty one means as its tooltip.
void rv_editor_run_path(const char *label,
    char *buf,
    size_t size,
    const char *empty_means_key,
    const rv_editor_app &app,
    bool can_fail,
    const rv_editor_theme &theme)
{
    rv_editor_form_custom_begin(label, theme);
    rv_editor_path_field((std::string("##") + label).c_str(), buf, size, app.project.root.string(), theme);
    if (buf[0] == '\0') {
        const auto empty_means = rv_editor_text(empty_means_key);
        const auto tip = rv_editor_text_format("pane_run_config.empty_means", std::make_format_args(empty_means));
        rv_editor_item_tooltip(tip.c_str());
    }
    rv_editor_form_custom_end(can_fail, nullptr, theme);
}

void rv_editor_run_check(const char *label, const char *tooltip, bool *value, const rv_editor_theme &theme)
{
    rv_editor_form_custom_begin(label, theme);
    rv_editor_checkbox("##check", value, theme);
    if (tooltip != nullptr) {
        rv_editor_item_tooltip(tooltip);
    }
    rv_editor_form_custom_end(false, nullptr, theme);
}

void rv_editor_run_area_text(const char *label, char *buf, size_t size, const rv_editor_theme &theme)
{
    rv_editor_form_custom_begin(label, theme);
    (void)rv_editor_text_area("##text", buf, size, multiline_box_lines, theme);
    rv_editor_form_custom_end(false, nullptr, theme);
}

// The profiles as rows; choosing one is itself applied: Run uses the one shown. The app makes each change.
void rv_editor_run_profiles(rv_editor_app &app, const rv_editor_theme &theme)
{
    const rv_editor_run_config &config = app.run_config;
    const char *why = rv_editor_run_profile_pending(app) ? rv_editor_text("app_why_not.request_pending") : nullptr;
    for (size_t i = 0; i < config.profiles.size(); ++i) {
        ImGui::PushID(static_cast<int>(i));
        const rv_editor_list_style style{ .selected = i == config.active };
        const rv_editor_list_result row =
            rv_editor_list_row("##profile", config.profiles[i].name.c_str(), theme, style, { rv_editor_look::live, why });
        if (row.clicked && i != config.active) {
            rv_editor_pane_request select = rv_editor_run_request(rv_editor_pane_request_kind::profile_select);
            select.choice = i;
            (void)rv_editor_app_request(app, select);
        }
        ImGui::PopID();
    }
}

// The shown profile's fields, as edited until Apply.
void rv_editor_run_fields(rv_editor_app &app, const rv_editor_theme &theme)
{
    rv_editor_run_form &form = run_form_draft;
    const std::string root = app.project.root.string();
    const rv_editor_run_rows paths{ rv_editor_path_relative(form.runtime, root),
        rv_editor_path_relative(form.cwd, root),
        rv_editor_path_relative(form.memcard, root) };
    rv_editor_form_begin(rv_editor_run_descs(form, paths), theme);
    (void)rv_editor_form_row(rv_editor_text("pane_run_config.name_label"),
        form.name,
        sizeof(form.name),
        nullptr,
        false,
        nullptr,
        theme);
    rv_editor_run_path(rv_editor_text("pane_run_config.runtime_label"),
        form.runtime,
        sizeof(form.runtime),
        "pane_run_config.runtime_empty_tooltip",
        app,
        true,
        theme);
    rv_editor_run_path(rv_editor_text("pane_run_config.working_dir_label"),
        form.cwd,
        sizeof(form.cwd),
        "pane_run_config.working_dir_empty_tooltip",
        app,
        false,
        theme);
    rv_editor_run_path(rv_editor_text("pane_run_config.memory_card_label"),
        form.memcard,
        sizeof(form.memcard),
        "pane_run_config.memory_card_empty_tooltip",
        app,
        false,
        theme);
    rv_editor_run_check(rv_editor_text("pane_run_config.start_paused_label"),
        rv_editor_text("pane_run_config.start_paused_tooltip"),
        &form.paused,
        theme);
    rv_editor_run_check(rv_editor_text("pane_run_config.fixed_step_label"),
        rv_editor_text("pane_run_config.fixed_step_tooltip"),
        &form.fixed_step,
        theme);
    rv_editor_run_check(rv_editor_text("pane_run_config.reload_on_save_label"),
        rv_editor_text("pane_run_config.reload_on_save_tooltip"),
        &form.reload_on_save,
        theme);
    rv_editor_run_check(rv_editor_text("pane_run_config.mute_label"), nullptr, &form.mute, theme);
    rv_editor_run_area_text(rv_editor_text("pane_run_config.console_options_key"), form.args, sizeof(form.args), theme);
    rv_editor_item_tooltip(rv_editor_text("pane_run_config.console_options_label"));
    rv_editor_run_area_text(rv_editor_text("pane_run_config.environment_key"), form.env, sizeof(form.env), theme);
    rv_editor_item_tooltip(rv_editor_text("pane_run_config.environment_label"));
    rv_editor_form_end();
}

// The row under the area: Apply, Apply and Restart and Revert, then a new profile and delete the shown one.
float rv_editor_run_buttons_width()
{
    float width = 0.0f;
    for (const char *key :
        { "pane_run_config.apply_label", "pane_run_config.apply_restart_label", "pane_run_config.revert_label" }) {
        width += rv_editor_button_width(rv_editor_text(key)) + ImGui::GetStyle().ItemSpacing.x;
    }
    return width + 2.0f * ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.x;
}

float rv_editor_run_content_width()
{
    return std::max(rv_editor_run_buttons_width(),
        rv_editor_message_box_min_width({ { rv_editor_text("panes.open_project_box") } }));
}

void rv_editor_run_actions(rv_editor_app &app,
    const rv_editor_run_profile &edited,
    const std::string &problem,
    const rv_editor_theme &theme)
{
    const rv_editor_run_config &config = app.run_config;
    const bool pending = rv_editor_run_profile_pending(app);
    const char *no_project = app.project.open ? nullptr : rv_editor_text("pane_files.no_project_open");
    const char *edit_why = no_project != nullptr ? no_project :
        problem.empty()                          ? nullptr :
                                                   rv_editor_text("pane_run_config.apply_disabled");
    const char *apply_why = no_project != nullptr ? no_project :
        pending                                   ? rv_editor_text("app_why_not.request_pending") :
                                                    edit_why;
    const bool apply =
        rv_editor_button(rv_editor_text("pane_run_config.apply_label"), theme, { rv_editor_look::live, apply_why });
    rv_editor_item_tooltip(rv_editor_text("pane_run_config.apply_button_tooltip"));
    ImGui::SameLine();
    const char *restart_why = no_project != nullptr ? no_project : rv_editor_run_restart_why(app, pending, edit_why);
    const bool restart =
        rv_editor_button(rv_editor_text("pane_run_config.apply_restart_label"), theme, { rv_editor_look::live, restart_why });
    rv_editor_item_tooltip(rv_editor_text("pane_run_config.apply_restart_tooltip"));
    ImGui::SameLine();
    if (rv_editor_button(rv_editor_text("pane_run_config.revert_label"), theme, { rv_editor_look::live, no_project }) &&
        app.project.open && !config.profiles.empty()) {
        rv_editor_run_form_fill(run_form_draft, config.profiles[config.active]);
    }
    const char *why = no_project != nullptr ? no_project : pending ? rv_editor_text("app_why_not.request_pending") : nullptr;
    ImGui::SameLine();
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
    if (!app.project.open) {
        return;
    }
    if (apply) {
        (void)rv_editor_app_request(app, rv_editor_run_request(rv_editor_pane_request_kind::profile_apply, edited));
    }
    if (restart) {
        (void)rv_editor_app_request(app, rv_editor_run_request(rv_editor_pane_request_kind::profile_apply_restart, edited));
    }
}

// What the strip under the area says: the profile file's error, the edited profile's problem and the session's note, or
// nothing (empty text).
struct rv_editor_run_say_text {
    std::string text;
    rv_editor_label_kind kind = rv_editor_label_kind::normal;
    std::vector<std::string> buttons;
};

rv_editor_run_say_text rv_editor_run_say(const rv_editor_app &app, const std::string &problem)
{
    if (!app.project.open) {
        return { rv_editor_text("panes.no_project_open_message"),
            rv_editor_label_kind::normal,
            { rv_editor_text("panes.open_project_box") } };
    }
    std::string text;
    if (!app.run_config.error.empty()) {
        text = rv_editor_text_format("pane_run_config.profile_error", std::make_format_args(app.run_config.error));
    }
    if (!problem.empty()) {
        text += (text.empty() ? "" : "\n") + problem;
    }
    if (text.empty() && app.session.live()) {
        const auto session_num = app.session.number();
        text = rv_editor_text_format("pane_run_config.session_active", std::make_format_args(session_num));
    }
    const bool error = !app.run_config.error.empty() || !problem.empty();
    return { text, error ? rv_editor_label_kind::error : rv_editor_label_kind::normal, {} };
}

} // namespace

rv_editor_size rv_editor_run_config_minimum()
{
    rv_editor_size size = rv_editor_tiled_minimum(rv_editor_run_content_width(), rv_editor_tiled_strip_block());
    // The row of buttons under the area.
    size.h += static_cast<int32_t>(std::ceil(ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.y));
    return size;
}

void rv_editor_pane_run_config(rv_editor_app &app, const rv_editor_theme &theme)
{
    rv_editor_run_config &config = app.run_config;
    const bool have_profile = app.project.open && !config.profiles.empty();
    if (have_profile && run_form_draft.loaded != app.run_config_revision) {
        rv_editor_run_form_fill(run_form_draft, config.profiles[std::min(config.active, config.profiles.size() - 1)]);
        run_form_draft.loaded = app.run_config_revision;
    }
    const rv_editor_run_profile edited = rv_editor_run_form_profile(run_form_draft);
    const std::string problem = have_profile ? rv_editor_app_profile_problem(app, edited) : std::string();
    const rv_editor_run_say_text say = rv_editor_run_say(app, problem);
    const float strip_w = std::max(ImGui::GetContentRegionAvail().x, rv_editor_run_content_width());
    const float strip_h = rv_editor_message_strip_height(say.text, say.buttons, strip_w);
    // The area leaves the strip and the row of buttons under it.
    const float height = ImGui::GetContentRegionAvail().y - ImGui::GetFrameHeight() - ImGui::GetStyle().ItemSpacing.y -
        (strip_h > 0.0f ? strip_h + ImGui::GetStyle().ItemSpacing.y : 0.0f);
    (void)rv_editor_tiled_area_begin("##run_area", rv_editor_run_content_width(), theme, {}, false, height);
    if (have_profile) {
        rv_editor_run_fields(app, theme);
        rv_editor_run_profiles(app, theme);
    } else {
        rv_editor_content_empty_row(rv_editor_text("panes.no_project_open_message"), theme);
    }
    rv_editor_tiled_area_end(theme);
    if (rv_editor_message_strip("##strip", say.text, say.kind, say.buttons, theme, strip_w) == 0 && !app.project.open) {
        app.open_folder_request = true;
    }
    rv_editor_run_actions(app, edited, problem, theme);
}

} // namespace rv_editor
