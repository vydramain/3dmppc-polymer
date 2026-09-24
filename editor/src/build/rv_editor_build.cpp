// The build job: mppcburner into a fresh directory, published only on success.

#include "build/rv_editor_build.hpp"

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

// The number a builds/<n> directory carries, or 0 for anything else.
uint32_t rv_editor_build_dir_number(const std::filesystem::path &dir)
{
    const std::string name = dir.filename().string();
    uint32_t n = 0;
    const auto [ptr, ec] = std::from_chars(name.data(), name.data() + name.size(), n);
    return ec == std::errc{} && ptr == name.data() + name.size() ? n : 0;
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
    std::string &error)
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
        dir_.string(), "--baker", tools.baker.path.string() };
    if (!proc_.start(argv, project.root, error)) {
        return false;
    }
    state_ = rv_editor_build_state::building;
    out_partial_.clear();
    err_partial_.clear();
    log.add(rv_editor_log_source::editor, rv_editor_log_level::info,
        "build #" + std::to_string(number_) + " started: " + tools.burner.path.string() + " build " +
            project.root.string() + " --unpacked " + dir_.string());
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
    proc_.read(out, err, 1 << 20);
    log.add_stream(rv_editor_log_source::build, out_partial_, out);
    log.add_stream(rv_editor_log_source::build, err_partial_, err);

    if (state_ == rv_editor_build_state::cancelling &&
        std::chrono::steady_clock::now() - cancel_at_ > rv_editor_cancel_grace) {
        proc_.stop(true);
    }
    if (!proc_.poll()) {
        return;
    }
    // Whatever the pipes still held when it ended.
    out.clear();
    err.clear();
    proc_.read(out, err, 1 << 20);
    log.add_stream(rv_editor_log_source::build, out_partial_, out);
    log.add_stream(rv_editor_log_source::build, err_partial_, err);
    log.flush_stream(rv_editor_log_source::build, out_partial_);
    log.flush_stream(rv_editor_log_source::build, err_partial_);

    const rv_editor_process::rv_editor_exit &exit = proc_.exit_status();
    const std::string label = "build #" + std::to_string(number_);
    if (state_ == rv_editor_build_state::building && exit.signal == 0 && exit.code == 0) {
        state_ = rv_editor_build_state::succeeded;
        last_success_ = rv_editor_artifact{ dir_, number_ };
        log.add(rv_editor_log_source::editor, rv_editor_log_level::info, label + " succeeded: " + dir_.string());
        return;
    }

    // A partial output is never a disc: it goes (BLD-06).
    std::error_code ec;
    std::filesystem::remove_all(dir_, ec);
    if (state_ == rv_editor_build_state::cancelling) {
        state_ = rv_editor_build_state::cancelled;
        log.add(rv_editor_log_source::editor, rv_editor_log_level::warning, label + " cancelled");
        return;
    }
    state_ = rv_editor_build_state::failed;
    log.add(rv_editor_log_source::editor, rv_editor_log_level::error,
        label + " failed: " + rv_editor_exit_text(exit));
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
    }
}

} // namespace rv_editor
