// The project a window works on, the tools it drives, and where its own files go.

#include "project/rv_editor_project.hpp"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <system_error>
#include <thread>

#include "pdk/rv_err.h"
#include "pdklib/rv_manifest/rv_manifest.hpp"
#include "pdklib/rv_manifest/rv_manifest_dialect.hpp"
#include "pdklib/rv_manifest/rv_manifest_pattern.hpp"
#include "platform/rv_editor_process.hpp"
#include "project/rv_editor_manifest_edit.hpp"
#include "text/rv_editor_text.hpp"

namespace rv_editor
{

namespace
{

// Stable across runs and builds, unlike std::hash: the directory name must
// find the same project's builds and card next time (0010).
std::string rv_editor_path_hash(const std::filesystem::path &path)
{
    uint64_t h = 0xcbf29ce484222325ull;
    for (const char c : path.string()) {
        h ^= static_cast<unsigned char>(c);
        h *= 0x100000001b3ull;
    }
    char buf[17];
    std::snprintf(buf, sizeof(buf), "%016llx", static_cast<unsigned long long>(h));
    return buf;
}

std::filesystem::path rv_editor_self_dir()
{
    std::error_code ec;
    const std::filesystem::path self = std::filesystem::read_symlink("/proc/self/exe", ec);
    return ec ? std::filesystem::path() : self.parent_path();
}

void rv_editor_tool_check(rv_editor_tool &tool, const char *name)
{
    std::error_code ec;
    const std::filesystem::file_status st = std::filesystem::status(tool.path, ec);
    if (tool.path.empty() || ec || !std::filesystem::exists(st)) {
        const std::string tool_name = name;
        const std::string tool_path = tool.path.string();
        if (tool.path.empty()) {
            tool.problem = rv_editor_text_format("project.tool_not_found",
                std::make_format_args(tool_name));
        } else {
            tool.problem = rv_editor_text_format("project.tool_not_found_at",
                std::make_format_args(tool_name, tool_path));
        }
        return;
    }
    if (!std::filesystem::is_regular_file(st) ||
        (st.permissions() & std::filesystem::perms::owner_exec) == std::filesystem::perms::none) {
        const std::string tool_path = tool.path.string();
        tool.problem = rv_editor_text_format("project.tool_not_executable",
            std::make_format_args(tool_path));
    }
}

// The first line `tool --version` prints, waiting at most a second for it.
void rv_editor_tool_version(rv_editor_tool &tool)
{
    if (!tool.problem.empty()) {
        return;
    }
    rv_editor_process proc;
    std::string error;
    if (proc.start({ tool.path.string(), "--version" }, {}, error) != RV_OK) {
        tool.version = error;
        return;
    }
    std::string out;
    std::string err;
    const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(1);
    while (!proc.poll() && std::chrono::steady_clock::now() < until) {
        proc.read(out, err, 4096);
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    proc.read(out, err, 4096);
    const rv_editor_process::rv_editor_exit exit = proc.exit_status();
    if (!exit.exited || exit.signal != 0 || exit.code != 0 || out.empty()) {
        tool.version = "no version: --version is not understood";
        return;
    }
    tool.version = out.substr(0, out.find('\n'));
}

} // namespace

std::filesystem::path rv_editor_xdg_dir(const char *var, const char *home_fallback)
{
    const char *xdg = std::getenv(var);
    if (xdg != nullptr && xdg[0] != '\0' && std::filesystem::path(xdg).is_absolute()) {
        return std::filesystem::path(xdg) / "3dmppc-editor";
    }
    const char *home = std::getenv("HOME");
    if (home != nullptr && home[0] != '\0' && std::filesystem::path(home).is_absolute()) {
        return std::filesystem::path(home) / home_fallback / "3dmppc-editor";
    }
    return {};
}

int rv_editor_revision_job::launch(const std::vector<std::string> &argv)
{
    proc_ = std::make_unique<rv_editor_process>();
    std::string error;
    out_.clear();
    until_ = std::chrono::steady_clock::now() + std::chrono::seconds(3);
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
    git_ = rv_editor_process_find("git");
    if (git_.empty()) {
        text_ = "unknown: git is not on PATH";
        return;
    }
    text_.clear();
    if (launch({ git_.string(), "rev-parse", "--verify", "HEAD" }) != RV_OK) {
        text_ = "unknown: not in a git work tree with a commit";
    }
}

bool rv_editor_revision_job::poll()
{
    if (!proc_) {
        return true;
    }
    std::string err;
    proc_->read(out_, err, 65536);
    const bool ended = proc_->poll();
    if (!ended && std::chrono::steady_clock::now() < until_) {
        return false;
    }
    proc_->read(out_, err, 65536);
    const rv_editor_process::rv_editor_exit exit = proc_->exit_status();
    const bool ok = exit.exited && exit.signal == 0 && exit.code == 0;
    proc_.reset();
    if (!status_step_) {
        if (!ok || out_.empty()) {
            text_ = "unknown: not in a git work tree with a commit";
            return true;
        }
        head_ = out_.substr(0, out_.find('\n'));
        status_step_ = true;
        // Only this project's files: a project may sit in a larger repository.
        if (launch({ git_.string(), "status", "--porcelain", "--", "." }) == RV_OK) {
            return false;
        }
        text_ = "git " + head_ + ", whether the files differ from it is unknown";
        return true;
    }
    if (!ok) {
        text_ = "git " + head_ + ", whether the files differ from it is unknown";
        return true;
    }
    text_ = "git " + head_ + (out_.empty() ? "" : " + uncommitted changes");
    return true;
}

rv_editor_toolchain rv_editor_toolchain_find()
{
    rv_editor_toolchain tc;
    const std::filesystem::path self = rv_editor_self_dir();
    tc.console = { self.empty() ? "" : self / "3dmppc", "next to the editor", "", "" };
    tc.burner = { self.empty() ? "" : self / "mppcburner", "next to the editor", "", "" };
    tc.baker = { self.empty() ? "" : self / "mppcbaker", "next to the editor", "", "" };

    const std::filesystem::path config = rv_editor_xdg_dir("XDG_CONFIG_HOME", ".config");
    if (!config.empty()) {
        tc.settings_path = config / "settings.toml";
        std::ifstream in(tc.settings_path, std::ios::binary);
        if (in) {
            std::ostringstream text;
            text << in.rdbuf();
            rv_pdklib::rv_manifest_tree tree;
            if (rv_pdklib::rv_manifest_read_tree(text.str(), tc.settings_path.string(), tree, tc.settings_error) == 0) {
                for (const auto &section : tree.sections) {
                    if (section.name != "tools") {
                        continue;
                    }
                    for (const auto &entry : section.entries) {
                        rv_editor_tool *tool = entry.key == "console" ? &tc.console
                            : entry.key == "burner"                   ? &tc.burner
                            : entry.key == "baker"                    ? &tc.baker
                            : entry.key == "player"                   ? &tc.player
                                                                      : nullptr;
                        if (tool != nullptr && entry.value.kind == rv_pdklib::rv_manifest_value_kind::string) {
                            *tool = { entry.value.str, "settings.toml", "", "" };
                        }
                    }
                }
            }
        }
    }

    rv_editor_tool_check(tc.console, "3dmppc");
    rv_editor_tool_check(tc.burner, "mppcburner");
    rv_editor_tool_check(tc.baker, "mppcbaker");
    rv_editor_tool_check(tc.player, "player");
    if (tc.player.path.empty()) {
        tc.player.problem = rv_editor_text("project.player_not_set_file_settings");
    }
    rv_editor_tool_version(tc.burner);
    rv_editor_tool_version(tc.baker);
    return tc;
}

rv_editor_tool rv_editor_tool_probe(const char *key, const std::filesystem::path &override_path)
{
    const std::string k = key;
    const char *exe = k == "console" ? "3dmppc" : k == "burner" ? "mppcburner" : k == "baker" ? "mppcbaker" : nullptr;
    const std::filesystem::path self = rv_editor_self_dir();
    rv_editor_tool tool;
    if (!override_path.empty()) {
        tool = { override_path, "settings.toml", "", "" };
    } else if (exe != nullptr && !self.empty()) {
        tool = { self / exe, "next to the editor", "", "" };
    }
    rv_editor_tool_check(tool, exe != nullptr ? exe : "player");
    if (tool.path.empty() && exe == nullptr) {
        tool.problem = rv_editor_text("project.player_not_set_name");
    }
    // As rv_editor_toolchain_find: only the burner and the baker are asked for a version.
    if (k == "burner" || k == "baker") {
        rv_editor_tool_version(tool);
    }
    return tool;
}

bool rv_editor_project_open(const std::filesystem::path &target, rv_editor_project &project, std::string &error)
{
    std::error_code ec;
    std::filesystem::path root = target;
    if (std::filesystem::is_regular_file(target, ec)) {
        if (target.filename() != "disc.toml") {
            error = target.string() + " is not a disc.toml";
            return false;
        }
        root = target.parent_path();
    }
    root = std::filesystem::canonical(root.empty() ? "." : root, ec);
    if (ec || !std::filesystem::is_directory(root, ec)) {
        error = target.string() + ": no such directory";
        return false;
    }
    if (!std::filesystem::is_regular_file(root / "disc.toml", ec)) {
        error = root.string() + " holds no disc.toml";
        return false;
    }

    rv_editor_project p;
    p.open = true;
    p.root = root;
    p.manifest = root / "disc.toml";
    const std::string hash = rv_editor_path_hash(root);
    const std::filesystem::path cache = rv_editor_xdg_dir("XDG_CACHE_HOME", ".cache");
    const std::filesystem::path state = rv_editor_xdg_dir("XDG_STATE_HOME", ".local/state");
    p.cache_dir = cache.empty() ? cache : cache / hash;
    p.state_dir = state.empty() ? state : state / hash;
    rv_editor_project_reload_manifest(p);
    project = std::move(p);
    return true;
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
    project.has_build_section = !manifest.build_sources.empty();
}

bool rv_editor_project_on_disc(const rv_editor_project &project, std::string_view rel)
{
    for (const auto *patterns : { &project.assets_patterns, &project.textures_patterns, &project.sounds_patterns }) {
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
    if (rel.ends_with(".png")) {
        return "textures";
    }
    if (rel.ends_with(".wav")) {
        return "sounds";
    }
    return "assets";
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
