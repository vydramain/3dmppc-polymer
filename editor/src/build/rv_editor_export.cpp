// The export job: mppcburner writes a disc, a player or a web export into the directory the user chose.

#include "build/rv_editor_export.hpp"

#include "pdk/rv_err.h"
#include "platform/rv_editor_process.hpp"
#include "text/rv_editor_text.hpp"

#include <cctype>
#include <cstddef>
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

// One export kind. The rows follow rv_editor_export_kind's order.
struct rv_editor_export_row {
    rv_editor_export_kind kind;
    std::string_view target;
    // -o is dest/<disc_id><output_suffix>; empty: -o is the destination directory itself.
    std::string_view output_suffix;
    // The result is dest/<result_name>; empty: the result is the output itself.
    std::string_view result_name;
};

constexpr rv_editor_export_row export_rows[] = {
    { rv_editor_export_kind::disc, "disc", disc_suffix, "" },
    { rv_editor_export_kind::player, "player", "", "" },
    { rv_editor_export_kind::web, "web", "", web_index_name },
};

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
    if (!tools.burner.problem.empty()) {
        error = tools.burner.problem;
        return RV_ERR_INVAL;
    }
    if (!tools.baker.problem.empty()) {
        error = tools.baker.problem;
        return RV_ERR_INVAL;
    }
    if (dest_dir.empty()) {
        error = rv_editor_text("export.no_destination");
        return RV_ERR_INVAL;
    }
    const rv_editor_export_row &row = export_rows[static_cast<std::size_t>(kind)];
    if (!row.output_suffix.empty() && !rv_editor_disc_id_is_file_name(project.disc_id)) {
        error = rv_editor_text("export.bad_disc_id");
        return RV_ERR_INVAL;
    }
    if (project.cache_dir.empty()) {
        error = rv_editor_text("build.no_cache_dir");
        return RV_ERR_INVAL;
    }

    std::error_code ec;
    std::filesystem::create_directories(dest_dir, ec);
    if (ec) {
        error = dest_dir.string() + ": " + ec.message();
        return RV_ERR_IO;
    }
    const std::filesystem::path output =
        row.output_suffix.empty() ? dest_dir : dest_dir / (project.disc_id + std::string(row.output_suffix));
    const std::filesystem::path result_path = row.result_name.empty() ? output : dest_dir / std::string(row.result_name);
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
    result_ = result_path;
    state_ = rv_editor_build_state::building;
    log.add(rv_editor_log_source::editor,
        rv_editor_log_level::info,
        "export #" + std::to_string(run) + " started: " + tools.burner.path.string() + " build " + project.root.string() +
            " --target " + std::string(row.target) + " -o " + output.string(),
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
    const std::string label = "export #" + std::to_string(run);
    if (state_ == rv_editor_build_state::succeeded) {
        log.add(rv_editor_log_source::editor,
            rv_editor_log_level::info,
            label + " written: " + result_.string(),
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
    log.add(rv_editor_log_source::editor,
        rv_editor_log_level::error,
        label + " failed: " + rv_editor_exit_text(job_.exit_status()),
        rv_editor_log_channel::none,
        pid,
        run);
    return rc;
}

} // namespace rv_editor
