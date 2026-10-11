// The project a window works on and where its own files go.

#include "project/rv_editor_project.hpp"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <system_error>

#include "pdk/rv_err.h"
#include "pdklib/rv_manifest/rv_manifest.hpp"
#include "pdklib/rv_manifest/rv_manifest_pattern.hpp"
#include "platform/rv_editor_process.hpp"
#include "project/rv_editor_manifest_edit.hpp"

namespace rv_editor
{

namespace
{

// FNV-1a hash: offset basis for the hash function.
constexpr uint64_t fnv_offset_basis = 0xcbf29ce484222325ull;
// FNV-1a hash: prime multiplier for each byte.
constexpr uint64_t fnv_prime = 0x100000001b3ull;
// Format specifier for 16-character hexadecimal hash representation.
constexpr std::string_view hash_format_spec = "%016llx";
// Buffer size for snprintf to hold a 16-digit hex hash and null terminator.
constexpr size_t hash_buf_size = 17;

// Manifest file name used by the project.
constexpr std::string_view manifest_file = "disc.toml";
// Directory name used for editor's cache and state subdirectories.
constexpr std::string_view editor_dir_name = "3dmppc-editor";

// Disc section names for assets, textures and sounds.
constexpr std::string_view disc_section_textures = "textures";
constexpr std::string_view disc_section_sounds = "sounds";
constexpr std::string_view disc_section_assets = "assets";
constexpr std::string_view disc_section_models = "models";

// File extensions for automatic section detection.
constexpr std::string_view file_ext_png = ".png";
constexpr std::string_view file_ext_wav = ".wav";

// XDG environment variables and fallback paths for user directories per XDG Base Directory spec.
constexpr std::string_view xdg_cache_var = "XDG_CACHE_HOME";
constexpr std::string_view xdg_cache_fallback = ".cache";
constexpr std::string_view xdg_state_var = "XDG_STATE_HOME";
constexpr std::string_view xdg_state_fallback = ".local/state";
// Standard environment variable for home directory.
constexpr std::string_view env_home = "HOME";

// Git command and arguments for revision and status queries.
constexpr std::string_view git_name = "git";
constexpr std::string_view git_rev_parse_cmd = "rev-parse";
constexpr std::string_view git_verify_arg = "--verify";
constexpr std::string_view git_head_ref = "HEAD";
constexpr std::string_view git_status_cmd = "status";
constexpr std::string_view git_porcelain_arg = "--porcelain";
constexpr std::string_view git_path_sep = "--";
constexpr std::string_view git_current_dir = ".";

// Current directory: the default project root when none is specified.
constexpr std::string_view current_dir = ".";

// Process I/O and timeout configuration for subprocess operations.
constexpr size_t process_read_large_buf_size = 65536;
constexpr int process_launch_timeout_sec = 3;

// Stable across runs and builds, unlike std::hash: the directory name must
// find the same project's builds and card next time (0010).
std::string rv_editor_path_hash(const std::filesystem::path &path)
{
    uint64_t h = fnv_offset_basis;
    for (const char c : path.string()) {
        h ^= static_cast<unsigned char>(c);
        h *= fnv_prime;
    }
    char buf[hash_buf_size];
    std::snprintf(buf, sizeof(buf), std::string(hash_format_spec).c_str(), static_cast<unsigned long long>(h));
    return buf;
}

} // namespace

std::filesystem::path rv_editor_xdg_dir(const char *var, const char *home_fallback)
{
    const char *xdg = std::getenv(var);
    if (xdg != nullptr && xdg[0] != '\0' && std::filesystem::path(xdg).is_absolute()) {
        return std::filesystem::path(xdg) / std::string(editor_dir_name);
    }
    const char *home = std::getenv(env_home.data());
    if (home != nullptr && home[0] != '\0' && std::filesystem::path(home).is_absolute()) {
        return std::filesystem::path(home) / home_fallback / std::string(editor_dir_name);
    }
    return {};
}

int rv_editor_revision_job::launch(const std::vector<std::string> &argv)
{
    proc_ = std::make_unique<rv_editor_process>();
    std::string error;
    out_.clear();
    until_ = std::chrono::steady_clock::now() + std::chrono::seconds(process_launch_timeout_sec);
    if (proc_->start(argv, root_, error) == RV_OK) {
        return RV_OK;
    }
    proc_.reset();
    return RV_ERR_IO;
}

void rv_editor_revision_job::start(const std::filesystem::path &root)
{
    root_ = root;
    proc_.reset();
    status_step_ = false;
    git_ = rv_editor_process_find(git_name.data());
    if (git_.empty()) {
        text_ = "unknown: git is not on PATH";
        return;
    }
    text_.clear();
    if (launch({ git_.string(), std::string(git_rev_parse_cmd), std::string(git_verify_arg), std::string(git_head_ref) }) !=
        RV_OK) {
        text_ = "unknown: not in a git work tree with a commit";
    }
}

bool rv_editor_revision_job::poll()
{
    if (!proc_) {
        return true;
    }
    std::string err;
    std::string read_error;
    // A failed read ends the job with its reason; the child is reaped by the process destructor.
    const auto read_failed = [this, &read_error]() {
        proc_.reset();
        text_ = "unknown: " + read_error;
        return true;
    };
    if (proc_->read(out_, err, process_read_large_buf_size, read_error) != RV_OK) {
        return read_failed();
    }
    const bool ended = proc_->poll();
    if (!ended && std::chrono::steady_clock::now() < until_) {
        return false;
    }
    if (proc_->read(out_, err, process_read_large_buf_size, read_error) != RV_OK) {
        return read_failed();
    }
    const rv_editor_process::rv_editor_exit exit = proc_->exit_status();
    const bool ok = exit.exited && exit.signal == 0 && exit.code == 0;
    proc_.reset();
    const std::string guard = exit.guard_result != RV_OK ? "; process guard: " + exit.guard_error : "";
    if (!status_step_) {
        // Nothing is read yet on this step: a guard failure ends the job as unknown.
        if (exit.guard_result != RV_OK) {
            text_ = "unknown: process guard: " + exit.guard_error;
            return true;
        }
        if (!ok || out_.empty()) {
            text_ = "unknown: not in a git work tree with a commit";
            return true;
        }
        head_ = out_.substr(0, out_.find('\n'));
        status_step_ = true;
        // Only this project's files: a project may sit in a larger repository.
        if (launch({ git_.string(),
                std::string(git_status_cmd),
                std::string(git_porcelain_arg),
                std::string(git_path_sep),
                std::string(git_current_dir) }) == RV_OK) {
            return false;
        }
        text_ = "git " + head_ + ", whether the files differ from it is unknown";
        return true;
    }
    if (!ok) {
        text_ = "git " + head_ + ", whether the files differ from it is unknown" + guard;
        return true;
    }
    text_ = "git " + head_ + (out_.empty() ? "" : " + uncommitted changes") + guard;
    return true;
}

int rv_editor_project_open(const std::filesystem::path &target, rv_editor_project &project, std::string &error)
{
    std::error_code ec;
    std::filesystem::path root = target;
    if (std::filesystem::is_regular_file(target, ec)) {
        if (target.filename() != std::string(manifest_file)) {
            error = target.string() + " is not a " + std::string(manifest_file);
            return RV_ERR_INVAL;
        }
        root = target.parent_path();
    }
    root = std::filesystem::canonical(root.empty() ? std::filesystem::path(current_dir) : root, ec);
    if (ec || !std::filesystem::is_directory(root, ec)) {
        error = target.string() + ": no such directory";
        return RV_ERR_NOENT;
    }
    if (!std::filesystem::is_regular_file(root / std::string(manifest_file), ec)) {
        error = root.string() + " holds no " + std::string(manifest_file);
        return RV_ERR_NOENT;
    }

    rv_editor_project p;
    p.open = true;
    p.root = root;
    p.manifest = root / std::string(manifest_file);
    const std::string hash = rv_editor_path_hash(root);
    const std::filesystem::path cache = rv_editor_xdg_dir(xdg_cache_var.data(), xdg_cache_fallback.data());
    const std::filesystem::path state = rv_editor_xdg_dir(xdg_state_var.data(), xdg_state_fallback.data());
    p.cache_dir = cache.empty() ? cache : cache / hash;
    p.state_dir = state.empty() ? state : state / hash;
    rv_editor_project_reload_manifest(p);
    project = std::move(p);
    return RV_OK;
}

void rv_editor_project_reload_manifest(rv_editor_project &project)
{
    rv_pdklib::rv_manifest manifest;
    std::string error;
    if (rv_pdklib::rv_manifest_load(project.manifest.string(), manifest, error) != 0) {
        project.manifest_error = error;
        project.assets_patterns.clear();
        project.textures_patterns.clear();
        project.sounds_patterns.clear();
        project.models_patterns.clear();
        project.has_build_section = false;
        return;
    }
    project.manifest_error.clear();
    project.disc_id = manifest.disc_id;
    project.disc_title = manifest.disc_title;
    project.screen_w = manifest.budget.pccv.screen_width;
    project.screen_h = manifest.budget.pccv.screen_height;
    project.assets_patterns = manifest.assets_files;
    project.textures_patterns = manifest.textures_files.files;
    project.sounds_patterns = manifest.sounds_files;
    project.models_patterns = manifest.models_files;
    project.has_build_section = !manifest.build_sources.empty();
}

bool rv_editor_project_on_disc(const rv_editor_project &project, std::string_view rel)
{
    for (const auto *patterns :
        { &project.assets_patterns, &project.textures_patterns, &project.sounds_patterns, &project.models_patterns }) {
        for (const std::string &pattern : *patterns) {
            if (rv_pdklib::rv_manifest_pattern_matches(pattern, rel)) {
                return true;
            }
        }
    }
    return false;
}

const char *rv_editor_project_disc_section(std::string_view rel)
{
    if (rel.ends_with(std::string(file_ext_png))) {
        return disc_section_textures.data();
    }
    if (rel.ends_with(std::string(file_ext_wav))) {
        return disc_section_sounds.data();
    }
    if (rel.ends_with(std::string(file_ext_glb))) {
        return disc_section_models.data();
    }
    return disc_section_assets.data();
}

int rv_editor_project_put_on_disc(rv_editor_project &project, std::string_view rel, std::string &error)
{
    const char *section = rv_editor_project_disc_section(rel);
    const int err = rv_editor_manifest_add_pattern(project.manifest, section, rel, error);
    if (err != RV_OK) {
        return err;
    }
    rv_editor_project_reload_manifest(project);
    return RV_OK;
}

} // namespace rv_editor
