// The build job: mppcburner into a fresh directory, published only on success.

#include "build/rv_editor_build.hpp"

#include "build/rv_editor_build_map.hpp"

#include <algorithm>
#include <charconv>
#include <system_error>
#include <vector>

namespace rv_editor
{

namespace
{

// A cancelled burner gets this long to stop by itself before it is killed.
constexpr auto rv_editor_cancel_grace = std::chrono::seconds(3);

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

std::vector<uint32_t> rv_editor_build_numbers(const std::filesystem::path &builds)
{
    std::vector<uint32_t> numbers;
    std::error_code ec;
    for (std::filesystem::directory_iterator it(builds, ec), end; !ec && it != end; it.increment(ec)) {
        const uint32_t n = rv_editor_build_dir_number(it->path());
        if (n != 0 && it->is_directory(ec)) {
            numbers.push_back(n);
        }
    }
    std::sort(numbers.begin(), numbers.end());
    return numbers;
}

} // namespace

const char *rv_editor_build_state_name(rv_editor_build_state state)
{
    switch (state) {
        case rv_editor_build_state::idle: return "not built";
        case rv_editor_build_state::building: return "building";
        case rv_editor_build_state::cancelling: return "cancelling";
        case rv_editor_build_state::succeeded: return "succeeded";
        case rv_editor_build_state::failed: return "failed";
        case rv_editor_build_state::cancelled: return "cancelled";
    }
    return "?";
}

bool rv_editor_build::start(const rv_editor_project &project, const rv_editor_toolchain &tools, rv_editor_log &log,
    std::string &error, const std::filesystem::path &image)
{
    if (busy()) {
        error = "a build is already running";
        return false;
    }
    if (!project.open) {
        error = "no project is open";
        return false;
    }
    if (!tools.burner.problem.empty()) {
        error = tools.burner.problem;
        return false;
    }
    if (!tools.baker.problem.empty()) {
        error = tools.baker.problem;
        return false;
    }
    if (project.cache_dir.empty()) {
        error = "no cache directory: neither XDG_CACHE_HOME nor HOME is set";
        return false;
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
            return false;
        }
        const std::vector<std::string> argv = { tools.burner.path.string(), "build", project.root.string(), "-o",
            image_.string(), "--baker", tools.baker.path.string(), "--map",
            rv_editor_build_map_path(image_).string() };
        if (!proc_.start(argv, project.root, error)) {
            return false;
        }
        state_ = rv_editor_build_state::building;
        out_partial_.clear();
        err_partial_.clear();
        log.add(rv_editor_log_source::editor, rv_editor_log_level::info,
            "candidate image started: " + tools.burner.path.string() + " build " + project.root.string() + " -o " +
                image_.string(),
            rv_editor_log_channel::none, proc_.pid(), rv_editor_image_number(image_));
        return true;
    }
    std::error_code ec;
    std::filesystem::create_directories(builds_, ec);
    if (ec) {
        error = builds_.string() + ": " + ec.message();
        return false;
    }
    const std::vector<uint32_t> numbers = rv_editor_build_numbers(builds_);
    number_ = numbers.empty() ? 1 : numbers.back() + 1;
    dir_ = builds_ / std::to_string(number_);

    const std::vector<std::string> argv = { tools.burner.path.string(), "build", project.root.string(), "--unpacked",
        dir_.string(), "--baker", tools.baker.path.string(), "--map", rv_editor_build_map_path(dir_).string() };
    if (!proc_.start(argv, project.root, error)) {
        return false;
    }
    state_ = rv_editor_build_state::building;
    out_partial_.clear();
    err_partial_.clear();
    log.add(rv_editor_log_source::editor, rv_editor_log_level::info,
        "build #" + std::to_string(number_) + " started: " + tools.burner.path.string() + " build " +
            project.root.string() + " --unpacked " + dir_.string(),
        rv_editor_log_channel::none, proc_.pid(), number_);
    return true;
}

void rv_editor_build::cancel()
{
    if (state_ != rv_editor_build_state::building) {
        return;
    }
    state_ = rv_editor_build_state::cancelling;
    cancel_at_ = std::chrono::steady_clock::now();
    proc_.stop(false);
}

void rv_editor_build::update(rv_editor_log &log)
{
    if (!busy()) {
        return;
    }
    std::string out;
    std::string err;
    const rv_editor_log_source source = image_.empty() ? rv_editor_log_source::build : rv_editor_log_source::candidate;
    const int64_t pid = proc_.pid();
    const uint32_t run = image_.empty() ? number_ : rv_editor_image_number(image_);
    proc_.read(out, err, 1 << 20);
    log.add_stream(source, out_partial_, out, rv_editor_log_channel::out, pid, run);
    log.add_stream(source, err_partial_, err, rv_editor_log_channel::err, pid, run);

    if (state_ == rv_editor_build_state::cancelling &&
        std::chrono::steady_clock::now() - cancel_at_ > rv_editor_cancel_grace) {
        proc_.stop(true);
    }
    if (!proc_.poll() || !proc_.output_done()) {
        return;
    }
    // Whatever the pipes still held when output completed.
    out.clear();
    err.clear();
    proc_.read(out, err, 1 << 20);
    log.add_stream(source, out_partial_, out, rv_editor_log_channel::out, pid, run);
    log.add_stream(source, err_partial_, err, rv_editor_log_channel::err, pid, run);
    log.flush_stream(source, out_partial_, rv_editor_log_channel::out, pid, run);
    log.flush_stream(source, err_partial_, rv_editor_log_channel::err, pid, run);
    if (proc_.output_cut()) {
        log.add(rv_editor_log_source::editor, rv_editor_log_level::warning,
            "burner output after exit was not fully read: a process it started kept a pipe open past the grace",
            rv_editor_log_channel::none, pid, run);
    }

    const rv_editor_process::rv_editor_exit &exit = proc_.exit_status();
    if (!image_.empty()) {
        const bool ok = state_ == rv_editor_build_state::building && exit.signal == 0 && exit.code == 0;
        state_ = ok ? rv_editor_build_state::succeeded
            : state_ == rv_editor_build_state::cancelling ? rv_editor_build_state::cancelled
                                                           : rv_editor_build_state::failed;
        if (!ok) {
            // A partial image is never a candidate (BLD-06).
            std::error_code ec;
            std::filesystem::remove(image_, ec);
        }
        log.add(rv_editor_log_source::editor, ok ? rv_editor_log_level::info : rv_editor_log_level::error,
            "candidate image " + image_.string() + (ok ? " written" : " not written: " + rv_editor_exit_text(exit)),
            rv_editor_log_channel::none, pid, run);
        return;
    }
    const std::string label = "build #" + std::to_string(number_);
    if (state_ == rv_editor_build_state::building && exit.signal == 0 && exit.code == 0) {
        state_ = rv_editor_build_state::succeeded;
        dev_state_ = state_;
        last_success_ = rv_editor_artifact{ dir_, number_ };
        log.add(rv_editor_log_source::editor, rv_editor_log_level::info, label + " succeeded: " + dir_.string(),
            rv_editor_log_channel::none, pid, run);
        return;
    }

    // A partial output is never a disc: it goes (BLD-06).
    std::error_code ec;
    std::filesystem::remove_all(dir_, ec);
    if (state_ == rv_editor_build_state::cancelling) {
        state_ = rv_editor_build_state::cancelled;
        dev_state_ = state_;
        log.add(rv_editor_log_source::editor, rv_editor_log_level::warning, label + " cancelled",
            rv_editor_log_channel::none, pid, run);
        return;
    }
    state_ = rv_editor_build_state::failed;
    dev_state_ = state_;
    log.add(rv_editor_log_source::editor, rv_editor_log_level::error,
        label + " failed: " + rv_editor_exit_text(exit), rv_editor_log_channel::none, pid, run);
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
    }
}

} // namespace rv_editor
