#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

#include "log/rv_editor_log.hpp"
#include "platform/rv_editor_process.hpp"
#include "project/rv_editor_project.hpp"

namespace rv_editor
{

enum class rv_editor_build_state
{
    idle,
    building,
    cancelling,
    succeeded,
    failed,
    cancelled,
};

// "not built", "building", ..., as the status bar and Controls show it.
const char *rv_editor_build_state_name(rv_editor_build_state state);

// A disc the burner finished, never rewritten once published: an unpacked
// directory under the project's cache, or a release candidate's image; the
// console takes either as its disc argument.
struct rv_editor_artifact
{
    std::filesystem::path dir;
    uint32_t number = 0;
};

// The build job (BLD-01): mppcburner in the background, writing each build to a
// fresh numbered directory, <cache>/builds/<n>. Only a build that exited 0 is
// published as the last successful artifact; a failed or cancelled one is
// deleted and never runs (BLD-05, BLD-06). The job lives outside the UI, so
// closing a tile does not cancel it (LAY-09), and it never touches the runtime
// (BLD-09).
class rv_editor_build
{
public:
    // False with the reason when a build cannot start now. With `image` the
    // burner writes that disc image (-o) instead of a numbered directory: a
    // release candidate, which Run never picks up.
    bool start(const rv_editor_project &project, const rv_editor_toolchain &tools, rv_editor_log &log,
        std::string &error, const std::filesystem::path &image = {});
    // Stops the burner and everything it started.
    void cancel();
    // Once a frame: moves output into the log and notices the end.
    void update(rv_editor_log &log);

    rv_editor_build_state state() const { return state_; }
    // How the last development build (not an image) went: what Run goes by.
    rv_editor_build_state dev_state() const { return dev_state_; }
    // The image the current or last job writes; empty for a development build.
    const std::filesystem::path &image() const { return image_; }
    bool busy() const { return state_ == rv_editor_build_state::building || state_ == rv_editor_build_state::cancelling; }
    uint32_t number() const { return number_; }
    const std::optional<rv_editor_artifact> &last_success() const { return last_success_; }
    // The burner's pid while a build runs; stale once it has ended.
    pid_t pid() const { return proc_.pid(); }
    // How the burner ended; only meaningful once the job is no longer building.
    const rv_editor_process::rv_editor_exit &exit_status() const { return proc_.exit_status(); }
    // True once the burner's output was cut short of end of file.
    bool output_cut() const { return proc_.output_cut(); }

    // Deletes published builds except the newest few and `in_use`. Only numbered
    // directories under this project's own builds directory are ever removed
    // (NFR-07).
    void prune(const std::filesystem::path &in_use);

private:
    rv_editor_process proc_;
    rv_editor_build_state state_ = rv_editor_build_state::idle;
    rv_editor_build_state dev_state_ = rv_editor_build_state::idle;
    std::filesystem::path image_;
    std::filesystem::path builds_;
    std::filesystem::path dir_;
    uint32_t number_ = 0;
    std::optional<rv_editor_artifact> last_success_;
    std::string out_partial_;
    std::string err_partial_;
    std::chrono::steady_clock::time_point cancel_at_{};
};

} // namespace rv_editor
