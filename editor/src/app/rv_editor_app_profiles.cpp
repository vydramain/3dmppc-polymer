// Run profiles the Run Configuration pane asks for: select, add, delete, apply and apply-restart.
// Each change goes through rv_editor_app_profiles_save, which writes the file and refills the form.

#include "app/rv_editor_app.hpp"

#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>

#include "pdk/rv_err.h"
#include "text/rv_editor_text.hpp"

namespace rv_editor
{

namespace
{

// The name a new profile starts with; the number after it makes it unique.
constexpr std::string_view new_profile_stem = "Profile";

// "ProfileN" with the first N from profile count + 1 that no profile has (a delete can leave a gap).
std::string rv_editor_app_profile_free_name(const rv_editor_app &app)
{
    const auto taken = [&app](const std::string &name) {
        return std::ranges::any_of(app.run_config.profiles, [&name](const rv_editor_run_profile &p) {
            return p.name == name;
        });
    };
    size_t number = app.run_config.profiles.size() + 1;
    std::string name = std::string(new_profile_stem) + std::to_string(number);
    while (taken(name)) {
        name = std::string(new_profile_stem) + std::to_string(++number);
    }
    return name;
}

// Saves the changed profiles; a failed save puts `before` back, so memory matches the file.
int rv_editor_app_profiles_commit(rv_editor_app &app, const rv_editor_run_config &before)
{
    const int err = rv_editor_app_profiles_save(app);
    if (err != RV_OK) {
        app.run_config = before;
        return err;
    }
    return RV_OK;
}

} // namespace

std::string rv_editor_app_profile_problem(const rv_editor_app &app, const rv_editor_run_profile &edited)
{
    std::string problem = rv_editor_run_profile_problem(edited, app.project.root);
    for (size_t i = 0; i < app.run_config.profiles.size() && problem.empty(); ++i) {
        if (i != app.run_config.active && app.run_config.profiles[i].name == edited.name) {
            problem = rv_editor_text_format("pane_run_config.profile_name_conflict", std::make_format_args(edited.name));
        }
    }
    return problem;
}

int rv_editor_app_profile_select(rv_editor_app &app, size_t index)
{
    if (index >= app.run_config.profiles.size()) {
        const std::string text = "profile select refused: no profile " + std::to_string(index);
        app.log.add(rv_editor_log_source::editor, rv_editor_log_level::error, text);
        return RV_ERR_INVAL;
    }
    if (index == app.run_config.active) {
        app.log.add(rv_editor_log_source::editor, rv_editor_log_level::warning, "profile is already the active one");
        return RV_ERR_ALREADY;
    }
    const rv_editor_run_config before = app.run_config;
    app.run_config.active = index;
    const int err = rv_editor_app_profiles_commit(app, before);
    if (err != RV_OK) {
        return err;
    }
    return RV_OK;
}

int rv_editor_app_profile_add(rv_editor_app &app)
{
    const rv_editor_run_config before = app.run_config;
    rv_editor_run_profile copy = app.run_config.profiles[app.run_config.active];
    copy.name = rv_editor_app_profile_free_name(app);
    app.run_config.profiles.push_back(std::move(copy));
    app.run_config.active = app.run_config.profiles.size() - 1;
    const int err = rv_editor_app_profiles_commit(app, before);
    if (err != RV_OK) {
        return err;
    }
    return RV_OK;
}

int rv_editor_app_profile_delete(rv_editor_app &app)
{
    if (app.run_config.profiles.size() == 1) {
        const std::string text = rv_editor_text("pane_run_config.delete_last_profile");
        app.log.add(rv_editor_log_source::editor, rv_editor_log_level::warning, text);
        return RV_ERR_INVAL;
    }
    const rv_editor_run_config before = app.run_config;
    app.run_config.profiles.erase(app.run_config.profiles.begin() + static_cast<std::ptrdiff_t>(app.run_config.active));
    app.run_config.active = 0;
    const int err = rv_editor_app_profiles_commit(app, before);
    if (err != RV_OK) {
        return err;
    }
    return RV_OK;
}

int rv_editor_app_profile_apply(rv_editor_app &app, const rv_editor_run_profile &edited)
{
    const std::string problem = rv_editor_app_profile_problem(app, edited);
    if (!problem.empty()) {
        app.log.add(rv_editor_log_source::editor, rv_editor_log_level::warning, "profile not applied: " + problem);
        return RV_ERR_INVAL;
    }
    const rv_editor_run_config before = app.run_config;
    app.run_config.profiles[app.run_config.active] = edited;
    const int err = rv_editor_app_profiles_commit(app, before);
    if (err != RV_OK) {
        return err;
    }
    return RV_OK;
}

int rv_editor_app_profile_apply_restart(rv_editor_app &app, const rv_editor_run_profile &edited)
{
    if (!app.session.live()) {
        const std::string text = "profile apply restart refused: no session runs; nothing applied";
        app.log.add(rv_editor_log_source::editor, rv_editor_log_level::warning, text);
        return RV_ERR_INVAL;
    }
    const int apply_err = rv_editor_app_profile_apply(app, edited);
    if (apply_err != RV_OK) {
        return apply_err;
    }
    const int restart_err = rv_editor_app_restart(app);
    if (restart_err != RV_OK) {
        return restart_err;
    }
    return RV_OK;
}

} // namespace rv_editor
