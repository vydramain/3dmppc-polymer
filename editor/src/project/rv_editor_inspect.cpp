// The background mppcinspector run and the problems its report gives.

#include "project/rv_editor_inspect.hpp"

#include <filesystem>
#include <format>
#include <string_view>
#include <system_error>

#include "pdk/rv_err.h"
#include "pdklib/rv_inspect/rv_inspect.hpp"
#include "pdklib/rv_inspect/rv_inspect_read.hpp"
#include "text/rv_editor_text.hpp"

namespace rv_editor
{

namespace
{

constexpr std::string_view inspector_name = "inspector";
constexpr std::string_view report_arg = "--report";
constexpr std::string_view report_file = "inspector.tsv";
// mppcinspector's exit codes that come with a report: 0 clean or warnings, 1 errors.
constexpr int exit_errors = 1;

} // namespace

int rv_editor_inspect::start(const rv_editor_project &project,
    const rv_editor_toolchain &tools,
    rv_editor_log &log,
    std::string &error)
{
    (void)log;
    problems_.clear();
    running_ = false;
    if (tools.inspector.path.empty() || !tools.inspector.problem.empty()) {
        return RV_OK;
    }
    if (project.cache_dir.empty()) {
        error = rv_editor_text("inspect.no_cache_dir");
        return RV_ERR_INVAL;
    }
    root_ = project.root;
    report_ = project.cache_dir / std::string(report_file);
    // The cache of a project not yet built does not exist; a failure shows as the missing report.
    std::error_code ec;
    std::filesystem::create_directories(project.cache_dir, ec);
    const int rc =
        job_.start({ tools.inspector.path.string(), project.root.string(), std::string(report_arg), report_.string() },
            project.root,
            std::string(inspector_name),
            rv_editor_log_source::editor,
            0,
            error);
    running_ = rc == RV_OK;
    return rc;
}

void rv_editor_inspect::update(rv_editor_log &log)
{
    if (!running_) {
        return;
    }
    std::string error;
    if (job_.pump(log, error) != RV_OK) {
        log.add(rv_editor_log_source::editor,
            rv_editor_log_level::error,
            rv_editor_text_format("inspect.not_started", std::make_format_args(error)));
    }
    if (!job_.ended()) {
        return;
    }
    running_ = false;
    finish(log);
}

void rv_editor_inspect::finish(rv_editor_log &log)
{
    const rv_editor_process::rv_editor_exit &exit = job_.exit_status();
    const int code = exit.signal != 0 ? exit.signal : exit.code;
    const bool with_report = exit.signal == 0 && (exit.code == 0 || exit.code == exit_errors);
    if (!with_report) {
        const std::string how =
            rv_editor_text_format(exit.signal != 0 ? "inspect.exit_signal" : "inspect.exit_code", std::make_format_args(code));
        log.add(rv_editor_log_source::editor,
            rv_editor_log_level::warning,
            rv_editor_text_format("inspect.failed", std::make_format_args(how)));
        return;
    }
    std::string text;
    std::string error;
    if (rv_pdklib::rv_inspect_read_text(report_, text, error) != RV_OK) {
        log.add(rv_editor_log_source::editor,
            rv_editor_log_level::warning,
            rv_editor_text_format("inspect.no_report", std::make_format_args(error)));
        return;
    }
    std::vector<rv_pdklib::rv_inspect_finding> found;
    if (rv_pdklib::rv_inspect_report_parse(text, found, error) != RV_OK) {
        log.add(rv_editor_log_source::editor,
            rv_editor_log_level::warning,
            rv_editor_text_format("inspect.bad_report", std::make_format_args(error)));
        return;
    }
    for (const rv_pdklib::rv_inspect_finding &f : found) {
        const std::string key = f.key.empty() ? "" : ", key " + f.key;
        problems_.push_back({ root_ / f.file,
            static_cast<int32_t>(f.line),
            0,
            f.severity == rv_pdklib::rv_inspect_severity::error,
            f.text + " [" + f.rule + key + "]",
            std::string(inspector_name) });
    }
}

} // namespace rv_editor
