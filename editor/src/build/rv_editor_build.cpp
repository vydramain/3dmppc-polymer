// The build job: mppcburner into a fresh directory, published only on success.

#include "build/rv_editor_build.hpp"

#include "build/rv_editor_build_map.hpp"

#include "pdk/rv_err.h"
#include "text/rv_editor_text.hpp"

#include <algorithm>
#include <charconv>
#include <system_error>
#include <vector>

namespace rv_editor
{

namespace
{

// The burner's name in the job's messages.
constexpr std::string_view burner_name = "burner";

// Build log file extension.
constexpr std::string_view build_log_ext = ".log";

// mppcburner command: build a disc or candidate image.
constexpr std::string_view build_command = "build";

// mppcburner flag: the directory of the bakers; the burner finds the baker for each asset kind there.
constexpr std::string_view build_flag_baker = "--baker";

// mppcburner flag: write the disc map to this file.
constexpr std::string_view build_flag_map = "--map";

// `name` as a whole number, or 0 if any of it is not a digit.
uint32_t rv_editor_parse_number(const std::string &name)
{
    uint32_t n = 0;
    const auto [ptr, ec] = std::from_chars(name.data(), name.data() + name.size(), n);
    return ec == std::errc{} && ptr == name.data() + name.size() ? n : 0;
}

// The number a builds/<n> directory carries, or 0 for anything else.
uint32_t rv_editor_build_dir_number(const std::filesystem::path &dir)
{
    return rv_editor_parse_number(dir.filename().string());
}

// The <n> a candidate image's own filename carries, or 0 for anything else.
uint32_t rv_editor_image_number(const std::filesystem::path &image)
{
    return rv_editor_parse_number(image.stem().string());
}

// A number is taken by either its directory <n> or a lone failed build's
// <n>.log (the editor's log sink writes that log before the burner makes its
// directory), so neither reuses a number the other already holds.
std::vector<uint32_t> rv_editor_build_numbers(const std::filesystem::path &builds)
{
    std::vector<uint32_t> numbers;
    std::error_code ec;
    for (std::filesystem::directory_iterator it(builds, ec), end; !ec && it != end; it.increment(ec)) {
        if (it->is_directory(ec)) {
            const uint32_t n = rv_editor_build_dir_number(it->path());
            if (n != 0) {
                numbers.push_back(n);
            }
            continue;
        }
        if (it->path().extension() == build_log_ext) {
            const uint32_t n = rv_editor_parse_number(it->path().stem().string());
            if (n != 0) {
                numbers.push_back(n);
            }
        }
    }
    std::sort(numbers.begin(), numbers.end());
    numbers.erase(std::unique(numbers.begin(), numbers.end()), numbers.end());
    return numbers;
}

} // namespace

const char *rv_editor_build_state_name(rv_editor_build_state state)
{
    switch (state) {
    case rv_editor_build_state::idle:
        return "not built";
    case rv_editor_build_state::building:
        return "building";
    case rv_editor_build_state::cancelling:
        return "cancelling";
    case rv_editor_build_state::succeeded:
        return "succeeded";
    case rv_editor_build_state::failed:
        return "failed";
    case rv_editor_build_state::cancelled:
        return "cancelled";
    }
    return "?";
}

int rv_editor_build::start(const rv_editor_project &project,
    const rv_editor_toolchain &tools,
    rv_editor_log &log,
    std::string &error,
    const std::filesystem::path &image)
{
    if (busy()) {
        error = rv_editor_text("build.already_running");
        return RV_ERR_BUSY;
    }
    if (!project.open) {
        error = rv_editor_text("build.no_project");
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
    if (project.cache_dir.empty()) {
        error = rv_editor_text("build.no_cache_dir");
        return RV_ERR_INVAL;
    }

    // A new project starts a new count; the numbers already on disk are kept.
    const std::filesystem::path builds = project.cache_dir / "builds";
    if (builds != builds_) {
        builds_ = builds;
        last_success_.reset();
        dev_state_ = rv_editor_build_state::idle;
    }
    image_ = image;
    if (!image_.empty()) {
        // An image is never overwritten: a number already used is refused.
        std::error_code ec;
        if (std::filesystem::exists(image_, ec)) {
            error = image_.string() + " already exists";
            return RV_ERR_INVAL;
        }
        const std::vector<std::string> argv = { tools.burner.path.string(),
            std::string(build_command),
            project.root.string(),
            "-o",
            image_.string(),
            std::string(build_flag_baker),
            tools.baker.path.string(),
            std::string(build_flag_map),
            rv_editor_build_map_path(image_).string() };
        const int err = job_.start(argv,
            project.root,
            std::string(burner_name),
            rv_editor_log_source::candidate,
            rv_editor_image_number(image_),
            error);
        if (err != RV_OK) {
            return err;
        }
        state_ = rv_editor_build_state::building;
        log.add(rv_editor_log_source::editor,
            rv_editor_log_level::info,
            "candidate image started: " + tools.burner.path.string() + " build " + project.root.string() + " -o " +
                image_.string(),
            rv_editor_log_channel::none,
            job_.pid(),
            rv_editor_image_number(image_));
        return RV_OK;
    }
    std::error_code ec;
    std::filesystem::create_directories(builds_, ec);
    if (ec) {
        error = builds_.string() + ": " + ec.message();
        return RV_ERR_IO;
    }
    const std::vector<uint32_t> numbers = rv_editor_build_numbers(builds_);
    number_ = numbers.empty() ? 1 : numbers.back() + 1;
    dir_ = builds_ / std::to_string(number_);

    const std::vector<std::string> argv = { tools.burner.path.string(),
        std::string(build_command),
        project.root.string(),
        "--unpacked",
        dir_.string(),
        std::string(build_flag_baker),
        tools.baker.path.string(),
        std::string(build_flag_map),
        rv_editor_build_map_path(dir_).string() };
    const int err = job_.start(argv, project.root, std::string(burner_name), rv_editor_log_source::build, number_, error);
    if (err != RV_OK) {
        return err;
    }
    state_ = rv_editor_build_state::building;
    log.add(rv_editor_log_source::editor,
        rv_editor_log_level::info,
        "build #" + std::to_string(number_) + " started: " + tools.burner.path.string() + " build " + project.root.string() +
            " --unpacked " + dir_.string(),
        rv_editor_log_channel::none,
        job_.pid(),
        number_);
    return RV_OK;
}

int rv_editor_build::cancel(std::string &error)
{
    if (state_ == rv_editor_build_state::cancelling) {
        error = "the build is already being cancelled";
        return RV_ERR_ALREADY;
    }
    if (state_ != rv_editor_build_state::building) {
        error = "no build is running";
        return RV_ERR_ALREADY;
    }
    state_ = rv_editor_build_state::cancelling;
    return job_.cancel(error);
}

int rv_editor_build::update(rv_editor_log &log, std::string &error)
{
    if (!busy()) {
        return RV_OK;
    }
    const uint32_t run = image_.empty() ? number_ : rv_editor_image_number(image_);
    const int result = job_.pump(log, error);
    if (!job_.ended()) {
        return result;
    }
    const int64_t pid = job_.pid();
    if (job_.output_cut()) {
        log.add(rv_editor_log_source::editor,
            rv_editor_log_level::warning,
            "burner output after exit was not fully read: a process it started kept a pipe open past the grace",
            rv_editor_log_channel::none,
            pid,
            run);
    }

    const rv_editor_process::rv_editor_exit &exit = job_.exit_status();
    const bool ok = job_.succeeded();
    if (!image_.empty()) {
        state_ = ok          ? rv_editor_build_state::succeeded :
            job_.cancelled() ? rv_editor_build_state::cancelled :
                               rv_editor_build_state::failed;
        if (!ok) {
            // A partial image is never a candidate.
            std::error_code ec;
            std::filesystem::remove(image_, ec);
        }
        log.add(rv_editor_log_source::editor,
            ok ? rv_editor_log_level::info : rv_editor_log_level::error,
            "candidate image " + image_.string() + (ok ? " written" : " not written: " + rv_editor_exit_text(exit)),
            rv_editor_log_channel::none,
            pid,
            run);
        return result;
    }
    const std::string label = "build #" + std::to_string(number_);
    if (ok) {
        state_ = rv_editor_build_state::succeeded;
        dev_state_ = state_;
        last_success_ = rv_editor_artifact{ dir_, number_ };
        log.add(rv_editor_log_source::editor,
            rv_editor_log_level::info,
            label + " succeeded: " + dir_.string(),
            rv_editor_log_channel::none,
            pid,
            run);
        return result;
    }

    // A partial output is never a disc: it goes.
    std::error_code ec;
    std::filesystem::remove_all(dir_, ec);
    if (job_.cancelled()) {
        state_ = rv_editor_build_state::cancelled;
        dev_state_ = state_;
        log.add(rv_editor_log_source::editor,
            rv_editor_log_level::warning,
            label + " cancelled",
            rv_editor_log_channel::none,
            pid,
            run);
        return result;
    }
    state_ = rv_editor_build_state::failed;
    dev_state_ = state_;
    log.add(rv_editor_log_source::editor,
        rv_editor_log_level::error,
        label + " failed: " + rv_editor_exit_text(exit),
        rv_editor_log_channel::none,
        pid,
        run);
    return result;
}

void rv_editor_build::prune(const std::filesystem::path &in_use)
{
    constexpr size_t keep = 3;
    if (builds_.empty() || busy()) {
        return;
    }
    const std::vector<uint32_t> numbers = rv_editor_build_numbers(builds_);
    for (size_t i = 0; i + keep < numbers.size(); ++i) {
        const std::filesystem::path dir = builds_ / std::to_string(numbers[i]);
        if (dir == in_use || (last_success_ && dir == last_success_->dir)) {
            continue;
        }
        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
        std::filesystem::remove(rv_editor_build_map_path(dir), ec);
        std::filesystem::remove(builds_ / (std::to_string(numbers[i]) + std::string(build_log_ext)), ec);
    }
}

} // namespace rv_editor
