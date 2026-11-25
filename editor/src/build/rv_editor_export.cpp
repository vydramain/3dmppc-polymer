// The export job: mppcburner writes a disc, a player or a web export into the directory the user chose.

#include "build/rv_editor_export.hpp"

#include "pdk/rv_err.h"
#include "platform/rv_editor_process.hpp"
#include "text/rv_editor_text.hpp"

#include <cctype>
#include <chrono>
#include <cstddef>
#include <format>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace rv_editor
{

namespace
{

// The burner's name in the job's messages.
constexpr std::string_view burner_name = "burner";

// mppcburner command: build a disc, a player or a web export.
constexpr std::string_view export_command = "build";

// mppcburner flags: the export kind, its output, and the directory of the bakers.
constexpr std::string_view export_flag_target = "--target";
constexpr std::string_view export_flag_output = "-o";
constexpr std::string_view export_flag_baker = "--baker";

// The export's build tree: a build may run at the same time and the burner deletes its tree at the end
// unless it is kept. A kept tree with a path also lets the next export build incrementally:
// cmake reconfigures it and ninja reuses its objects.
constexpr std::string_view export_flag_keep_build = "--keep-build=";
constexpr std::string_view export_build_subdir = "export-build";

// The disc file's extension: a disc export writes dest/<disc_id>.mppcdisc.
constexpr std::string_view disc_suffix = ".mppcdisc";

// The web export's page: the result the user opens in a browser.
constexpr std::string_view web_index_name = "index.html";

// The job's names in the log: an export, or the setup that installs the web tools.
constexpr std::string_view export_label = "export";
constexpr std::string_view web_setup_label = "web setup";

// mppcburner setup <tool>: installs a pinned tool once per user. It needs no project, so it runs in the burner's directory.
constexpr std::string_view setup_command = "setup";
constexpr std::string_view setup_tool_web = "web";

// One row per export kind, in rv_editor_export_kind order.
constexpr rv_editor_export_kind_info export_rows[] = {
    { rv_editor_export_kind::disc,
        "mppcdisc",
        false,
        rv_editor_export_open_action::folder,
        "export.disc_name",
        "export.disc_description",
        "disc",
        disc_suffix,
        "",
        false },
    { rv_editor_export_kind::player,
        "bundle",
        false,
        rv_editor_export_open_action::folder,
        "export.bundle_name",
        "export.bundle_description",
        "player",
        "",
        "",
        true },
    { rv_editor_export_kind::web,
        "web",
        true,
        rv_editor_export_open_action::browser,
        "export.web_name",
        "export.web_description",
        "web",
        "",
        web_index_name,
        false },
};

// The directory under the project root all exports go to.
constexpr std::string_view export_dir_name = "export";

// Every row sits at its kind's index, so export_rows[kind] is that kind's row.
constexpr bool export_rows_in_kind_order()
{
    for (std::size_t i = 0; i < std::size(export_rows); ++i) {
        if (static_cast<std::size_t>(export_rows[i].kind) != i) {
            return false;
        }
    }
    return true;
}

static_assert(export_rows_in_kind_order());
static_assert(std::size(export_rows) == static_cast<std::size_t>(rv_editor_export_kind::web) + 1);

// A disc id is a file name: letters, digits, '-' and '_'. disc.toml is not checked on load, so the editor checks it here.
bool rv_editor_disc_id_is_file_name(const std::string &id)
{
    if (id.empty()) {
        return false;
    }
    for (const char c : id) {
        const unsigned char u = static_cast<unsigned char>(c);
        if (std::isalnum(u) == 0 && c != '-' && c != '_') {
            return false;
        }
    }
    return true;
}

// The state a job that has ended ends in.
rv_editor_build_state rv_editor_export_end_state(const rv_editor_job &job)
{
    if (job.succeeded()) {
        return rv_editor_build_state::succeeded;
    }
    if (job.cancelled()) {
        return rv_editor_build_state::cancelled;
    }
    return rv_editor_build_state::failed;
}

} // namespace

int rv_editor_export::start(const rv_editor_project &project,
    const rv_editor_toolchain &tools,
    rv_editor_export_kind kind,
    const std::filesystem::path &dest_dir,
    rv_editor_log &log,
    std::string &error)
{
    if (busy()) {
        error = rv_editor_text("export.busy");
        return RV_ERR_BUSY;
    }
    if (!project.open) {
        error = rv_editor_text("export.no_project");
        return RV_ERR_INVAL;
    }
    const rv_editor_export_kind_info &row = export_rows[static_cast<std::size_t>(kind)];
    if (!tools.burner.problem.empty()) {
        error = tools.burner.problem;
        return RV_ERR_INVAL;
    }
    if (!tools.baker.problem.empty()) {
        error = tools.baker.problem;
        return RV_ERR_INVAL;
    }
    if (row.needs_firmware && !tools.firmware.problem.empty()) {
        error = tools.firmware.problem;
        return RV_ERR_INVAL;
    }
    if (dest_dir.empty()) {
        error = rv_editor_text("export.no_destination");
        return RV_ERR_INVAL;
    }
    if (!row.output_suffix.empty() && !rv_editor_disc_id_is_file_name(project.disc_id)) {
        error = rv_editor_text("export.bad_disc_id");
        return RV_ERR_INVAL;
    }
    if (project.cache_dir.empty()) {
        error = rv_editor_text("build.no_cache_dir");
        return RV_ERR_INVAL;
    }

    // mppcburner runs in the project root and the field offers <root>/export, so a relative destination is
    // relative to the root. The editor, the burner and the log then name the same directory.
    const std::filesystem::path abs_dest = (dest_dir.is_absolute() ? dest_dir : project.root / dest_dir).lexically_normal();

    // An export empties a marked directory, so one that is or holds the project (or the home, or /) is never offered.
    std::error_code ec;
    const std::filesystem::path real_root = std::filesystem::weakly_canonical(project.root, ec);
    const std::filesystem::path under = real_root.lexically_relative(std::filesystem::weakly_canonical(abs_dest, ec));
    if (!ec && !under.empty() && *under.begin() != "..") {
        error = rv_editor_text("export.dest_holds_project");
        return RV_ERR_INVAL;
    }

    const int prepared = rv_editor_export_prepare_dir(abs_dest, error);
    if (prepared != RV_OK) {
        return prepared;
    }
    // The mark is written before the burner runs: a failed export still leaves the directory ours to replace.
    {
        std::ofstream mark(abs_dest / std::string(rv_editor_export_mark_name));
        if (!mark) {
            const std::string name = abs_dest.string();
            error = rv_editor_text_format("export.cannot_mark", std::make_format_args(name));
            return RV_ERR_IO;
        }
    }
    const std::filesystem::path output =
        row.output_suffix.empty() ? abs_dest : abs_dest / (project.disc_id + std::string(row.output_suffix));
    const std::filesystem::path result_path = row.result_name.empty() ? output : abs_dest / std::string(row.result_name);
    const uint32_t run = runs_ + 1;
    const std::filesystem::path build_dir = project.cache_dir / std::string(export_build_subdir);

    const std::vector<std::string> argv = { tools.burner.path.string(),
        std::string(export_command),
        project.root.string(),
        std::string(export_flag_target),
        std::string(row.target),
        std::string(export_flag_output),
        output.string(),
        std::string(export_flag_baker),
        tools.baker.path.string(),
        std::string(export_flag_keep_build) + build_dir.string() };
    const int err = job_.start(argv, project.root, std::string(burner_name), rv_editor_log_source::build, run, error);
    if (err != RV_OK) {
        return err;
    }
    runs_ = run;
    started_at_ = std::chrono::steady_clock::now();
    result_ = result_path;
    failure_reason_.clear();
    label_ = std::string(export_label);
    setup_job_ = false;
    state_ = rv_editor_build_state::building;
    log.add(rv_editor_log_source::editor,
        rv_editor_log_level::info,
        label_ + " #" + std::to_string(run) + " started: " + tools.burner.path.string() + " build " + project.root.string() +
            " --target " + std::string(row.target) + " -o " + output.string(),
        rv_editor_log_channel::none,
        job_.pid(),
        run);
    return RV_OK;
}

int rv_editor_export::start_setup(const rv_editor_toolchain &tools, rv_editor_log &log, std::string &error)
{
    if (busy()) {
        error = rv_editor_text("export.busy");
        return RV_ERR_BUSY;
    }
    if (!tools.burner.problem.empty()) {
        error = tools.burner.problem;
        return RV_ERR_INVAL;
    }

    const uint32_t run = runs_ + 1;
    const std::filesystem::path burner_dir = tools.burner.path.parent_path();
    const std::vector<std::string> argv = { tools.burner.path.string(),
        std::string(setup_command),
        std::string(setup_tool_web) };
    const int err = job_.start(argv, burner_dir, std::string(burner_name), rv_editor_log_source::build, run, error);
    if (err != RV_OK) {
        return err;
    }
    runs_ = run;
    started_at_ = std::chrono::steady_clock::now();
    result_.clear();
    failure_reason_.clear();
    label_ = std::string(web_setup_label);
    setup_job_ = true;
    state_ = rv_editor_build_state::building;
    log.add(rv_editor_log_source::editor,
        rv_editor_log_level::info,
        label_ + " #" + std::to_string(run) + " started: " + tools.burner.path.string() + " " + std::string(setup_command) +
            " " + std::string(setup_tool_web),
        rv_editor_log_channel::none,
        job_.pid(),
        run);
    return RV_OK;
}

int rv_editor_export::cancel(std::string &error)
{
    if (state_ == rv_editor_build_state::cancelling) {
        error = rv_editor_text("export.already_cancelling");
        return RV_ERR_ALREADY;
    }
    if (state_ != rv_editor_build_state::building) {
        error = rv_editor_text("export.no_export_running");
        return RV_ERR_ALREADY;
    }
    state_ = rv_editor_build_state::cancelling;
    return job_.cancel(error);
}

int rv_editor_export::update(rv_editor_log &log, std::string &error)
{
    if (!busy()) {
        return RV_OK;
    }
    const int rc = job_.pump(log, error);
    if (!job_.ended()) {
        return rc;
    }
    const int64_t pid = job_.pid();
    const uint32_t run = runs_;
    if (job_.output_cut()) {
        log.add(rv_editor_log_source::editor,
            rv_editor_log_level::warning,
            "burner output after exit was not fully read: a process it started kept a pipe open past the grace",
            rv_editor_log_channel::none,
            pid,
            run);
    }

    state_ = rv_editor_export_end_state(job_);
    ended_at_ = std::chrono::steady_clock::now();
    const std::string label = label_ + " #" + std::to_string(run);
    if (state_ == rv_editor_build_state::succeeded) {
        // A setup has no result path: it only reports that it is done.
        const std::string outcome = result_.empty() ? std::string(" done") : " written: " + result_.string();
        log.add(rv_editor_log_source::editor,
            rv_editor_log_level::info,
            label + outcome,
            rv_editor_log_channel::none,
            pid,
            run);
        return rc;
    }
    if (state_ == rv_editor_build_state::cancelled) {
        log.add(rv_editor_log_source::editor,
            rv_editor_log_level::warning,
            label + " cancelled",
            rv_editor_log_channel::none,
            pid,
            run);
        return rc;
    }
    // The burner's error is its last stderr line; a process that printed none has only its exit text.
    const bool stderr_reason = !job_.last_stderr_line().empty();
    const std::string exit_text = rv_editor_exit_text(job_.exit_status());
    failure_reason_ = stderr_reason ? job_.last_stderr_line() : exit_text;
    const std::string exit_detail = stderr_reason ? " (" + exit_text + ")" : std::string();
    log.add(rv_editor_log_source::editor,
        rv_editor_log_level::error,
        label + " failed: " + failure_reason_ + exit_detail,
        rv_editor_log_channel::none,
        pid,
        run);
    return rc;
}

void rv_editor_export::clear_finished_result()
{
    if (busy() || setup_job()) {
        return;
    }
    state_ = rv_editor_build_state::idle;
    result_.clear();
    failure_reason_.clear();
    label_.clear();
    started_at_ = {};
    ended_at_ = {};
}

std::span<const rv_editor_export_kind_info> rv_editor_export_kinds()
{
    return export_rows;
}

std::filesystem::path rv_editor_export_default_dir(const std::filesystem::path &project_root, rv_editor_export_kind kind)
{
    return project_root / export_dir_name / export_rows[static_cast<std::size_t>(kind)].subdir;
}

std::string rv_editor_export_paths::get(const std::filesystem::path &project_root, rv_editor_export_kind kind) const
{
    const std::string &edited = edited_[static_cast<std::size_t>(kind)];
    return edited.empty() ? rv_editor_export_default_dir(project_root, kind).string() : edited;
}

void rv_editor_export_paths::set(rv_editor_export_kind kind, const std::string &path)
{
    edited_[static_cast<std::size_t>(kind)] = path;
}

void rv_editor_export_paths::reset()
{
    edited_ = {};
}

int rv_editor_export_prepare_dir(const std::filesystem::path &dir, std::string &error)
{
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::create_directories(dir, ec);
    if (ec || !fs::is_directory(dir, ec)) {
        error = dir.string() + ": " + (ec ? ec.message() : std::string(rv_editor_text("export.not_a_directory")));
        return RV_ERR_IO;
    }
    const bool empty = fs::is_empty(dir, ec);
    if (ec) {
        error = dir.string() + ": " + ec.message();
        return RV_ERR_IO;
    }
    if (empty) {
        return RV_OK;
    }
    // Only a directory with the mark is ours to empty; anything else is the user's and stays untouched.
    if (!fs::is_regular_file(fs::symlink_status(dir / std::string(rv_editor_export_mark_name), ec))) {
        const std::string name = dir.string();
        error = rv_editor_text_format("export.dir_not_empty", std::make_format_args(name));
        return RV_ERR_INVAL;
    }
    for (const auto &entry : fs::directory_iterator(dir, ec)) {
        fs::remove_all(entry.path(), ec);
        if (ec) {
            error = entry.path().string() + ": " + ec.message();
            return RV_ERR_IO;
        }
    }
    if (ec) {
        error = dir.string() + ": " + ec.message();
        return RV_ERR_IO;
    }
    return RV_OK;
}

bool rv_editor_export_setup_bar_due(const rv_editor_export &exporter)
{
    return exporter.busy() && exporter.setup_job();
}

} // namespace rv_editor
