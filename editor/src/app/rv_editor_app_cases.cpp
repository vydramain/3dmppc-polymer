// Test Case listing: the project's testcases/*.txt read by the app, which logs what it cannot list,
// and the question whether the listing is stale. The Test Case pane draws the cached cases.

#include "app/rv_editor_app.hpp"

#include <algorithm>
#include <string>
#include <system_error>
#include <vector>

#include "pdk/rv_err.h"

namespace rv_editor
{

namespace
{

// Reads the directory's cases into listed: each readable one with its text and file time. An unreadable case
// is logged and left out. RV_OK, or RV_ERR_IO with the reason in error when the directory cannot be walked.
int rv_editor_cases_scan(rv_editor_app &app,
    const std::filesystem::path &dir,
    std::vector<rv_editor_listed_case> &listed,
    std::vector<rv_editor_listed_case> &skipped,
    std::string &error)
{
    std::error_code ec;
    std::filesystem::directory_iterator it(dir, ec);
    for (; !ec && it != std::filesystem::directory_iterator(); it.increment(ec)) {
        const std::filesystem::path path = it->path();
        if (path.extension() == file_ext_text) {
            rv_editor_case c;
            std::string reason;
            const int read_rv = rv_editor_case_read(path, c, reason);
            std::error_code file_ec;
            const std::filesystem::file_time_type stamp = std::filesystem::last_write_time(path, file_ec);
            // The sentinel {} marks a file that could not be stat'ed: a stat that fails again is no change.
            const std::filesystem::file_time_type written = file_ec ? std::filesystem::file_time_type{} : stamp;
            const bool readable = read_rv == RV_OK && !file_ec;
            if (readable) {
                listed.push_back({ path, c.title, c.steps, c.expected, written });
            } else {
                const std::string why = read_rv == RV_OK ? "cannot check " + path.string() + ": " + file_ec.message() : reason;
                app.log.add(rv_editor_log_source::editor, rv_editor_log_level::warning, "test case skipped: " + why);
                skipped.push_back({ path, {}, {}, {}, written });
            }
        }
    }
    if (ec) {
        error = "test cases not listed: " + ec.message();
        return RV_ERR_IO;
    }
    return RV_OK;
}

} // namespace

// The listing; see rv_editor_app_findings.hpp.
int rv_editor_app_cases_list(rv_editor_app &app)
{
    rv_editor_findings &f = app.findings;
    const std::filesystem::path dir = app.project.root / std::string(dir_name_testcases);
    std::error_code ec;
    const std::filesystem::file_time_type at = std::filesystem::last_write_time(dir, ec);
    // A project without testcases/ is normal: no error and nothing listed.
    const bool missing = ec == std::errc::no_such_file_or_directory;
    const bool present = !ec;
    const bool unreadable = !present && !missing;
    // The chosen case stays chosen when the new listing still has it.
    const bool has_chosen = f.test_case >= 0 && static_cast<size_t>(f.test_case) < f.cases.size();
    const std::filesystem::path chosen = has_chosen ? f.cases[static_cast<size_t>(f.test_case)].path : std::filesystem::path();
    std::vector<rv_editor_listed_case> listed;
    std::vector<rv_editor_listed_case> skipped;
    std::string error = unreadable ? "test cases not listed: " + ec.message() : std::string();
    const int scan_rv = present ? rv_editor_cases_scan(app, dir, listed, skipped, error) : RV_OK;
    const int rv = unreadable ? RV_ERR_IO : (missing ? RV_ERR_NOENT : scan_rv);
    const bool failed = rv == RV_ERR_IO;
    f.cases = rv == RV_OK ? std::move(listed) : std::vector<rv_editor_listed_case>();
    f.skipped = rv == RV_OK ? std::move(skipped) : std::vector<rv_editor_listed_case>();
    std::ranges::sort(f.cases, {}, &rv_editor_listed_case::path);
    const auto found = std::ranges::find(f.cases, chosen, &rv_editor_listed_case::path);
    f.test_case = chosen.empty() || found == f.cases.end() ? -1 : static_cast<int32_t>(found - f.cases.begin());
    f.cases_read = present ? at : std::filesystem::file_time_type{};
    f.cases_failed = unreadable;
    if (rv != RV_OK) {
        if (failed) {
            app.log.add(rv_editor_log_source::editor, rv_editor_log_level::warning, error);
        }
        return rv;
    }
    return RV_OK;
}

// Question: see rv_editor_app_findings.hpp. One stat per listed or skipped case per frame.
bool rv_editor_app_cases_stale(const rv_editor_app &app)
{
    const rv_editor_findings &f = app.findings;
    const std::filesystem::path dir = app.project.root / std::string(dir_name_testcases);
    std::error_code ec;
    const std::filesystem::file_time_type at = std::filesystem::last_write_time(dir, ec);
    if (ec) {
        // No directory: a listing is due only to clear the cases still listed. Unreadable time: due once, unless it failed.
        const bool missing = ec == std::errc::no_such_file_or_directory;
        return missing ? !f.cases.empty() : !f.cases_failed;
    }
    // A failed walk still records the directory's time, so it is retried only when the directory changes.
    // A stat that fails counts as a change once: a sentinel written ({}) means it already failed.
    const auto changed = [](const rv_editor_listed_case &c) {
        std::error_code file_ec;
        const std::filesystem::file_time_type now = std::filesystem::last_write_time(c.path, file_ec);
        return file_ec ? c.written != std::filesystem::file_time_type{} : now != c.written;
    };
    const bool moved = std::ranges::any_of(f.cases, changed) || std::ranges::any_of(f.skipped, changed);
    return at != f.cases_read || moved;
}

} // namespace rv_editor
