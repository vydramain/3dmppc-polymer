// S20 (review of S15): the profile and findings handlers called with no project open do nothing: no file anywhere, the
// profiles stay as they were, and the call returns. They are called directly, as a pane's request reaches them.

#include <filesystem>
#include <string>
#include <system_error>

#include <unistd.h>

#include "app/rv_editor_shell.hpp"
#include "app/rv_editor_app_findings.hpp"
#include "rv_editor_test.hpp"

using namespace rv_editor;
namespace fs = std::filesystem;

namespace
{

// Runs in a fresh empty directory (a handler that forgot the project writes into the working directory) and gives back
// the number of entries it left there.
struct empty_cwd {
    fs::path before = fs::current_path();
    fs::path where = fs::temp_directory_path() / ("rv_editor_s20_no_project_" + std::to_string(::getpid()));
    empty_cwd()
    {
        std::error_code ec;
        fs::remove_all(where, ec);
        fs::create_directories(where, ec);
        fs::current_path(where, ec);
    }
    ~empty_cwd()
    {
        std::error_code ec;
        fs::current_path(before, ec);
        fs::remove_all(where, ec);
    }
    empty_cwd(const empty_cwd &) = delete;
    empty_cwd &operator=(const empty_cwd &) = delete;
    size_t entries() const
    {
        size_t n = 0;
        std::error_code ec;
        for (auto it = fs::recursive_directory_iterator(where, ec); !ec && it != fs::recursive_directory_iterator(); ++it) {
            ++n;
        }
        return n;
    }
};

rv_editor_run_profile profile(const std::string &name)
{
    rv_editor_run_profile p;
    p.name = name;
    p.mute = true; // a valid profile that differs from the default one
    return p;
}

} // namespace

RV_EDITOR_TEST(rv_editor_test_s20_profile_handlers_do_nothing_without_a_project)
{
    empty_cwd cwd;
    rv_editor_shell shell;
    rv_editor_app &app = shell.app;
    RV_EDITOR_CHECK(!app.project.open);
    app.run_config.profiles.push_back(profile("second"));
    const auto before = app.run_config.profiles;
    const size_t active = app.run_config.active;

    (void)rv_editor_app_profile_add(app);
    RV_EDITOR_CHECK(app.run_config.profiles.size() == before.size());
    (void)rv_editor_app_profile_delete(app);
    RV_EDITOR_CHECK(app.run_config.profiles.size() == before.size());
    (void)rv_editor_app_profile_apply(app, profile("edited"));
    RV_EDITOR_CHECK(app.run_config.active == active);
    for (size_t i = 0; i < before.size() && i < app.run_config.profiles.size(); ++i) {
        RV_EDITOR_CHECK(app.run_config.profiles[i].name == before[i].name);
        RV_EDITOR_CHECK(app.run_config.profiles[i].mute == before[i].mute);
    }
    RV_EDITOR_CHECK(cwd.entries() == 0);
}

RV_EDITOR_TEST(rv_editor_test_s20_findings_handlers_do_nothing_without_a_project)
{
    empty_cwd cwd;
    rv_editor_shell shell;
    rv_editor_app &app = shell.app;
    RV_EDITOR_CHECK(!app.project.open);
    const size_t saved = app.findings.saved.size();
    const size_t requests = app.open_requests.size();

    (void)rv_editor_app_save_finding(app);
    RV_EDITOR_CHECK(app.findings.saved.size() == saved);
    (void)rv_editor_app_case_new(app);
    RV_EDITOR_CHECK(app.open_requests.size() == requests);
    RV_EDITOR_CHECK(cwd.entries() == 0);
}
