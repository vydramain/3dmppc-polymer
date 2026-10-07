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

// FNV-1a hash: offset basis for the hash function.
constexpr uint64_t fnv_offset_basis = 0xcbf29ce484222325ull;
// FNV-1a hash: prime multiplier for each byte.
constexpr uint64_t fnv_prime = 0x100000001b3ull;
// Format specifier for 16-character hexadecimal hash representation.
constexpr std::string_view hash_format_spec = "%016llx";
// Buffer size for snprintf to hold a 16-digit hex hash and null terminator.
constexpr size_t hash_buf_size = 17;

// Symlink path to the currently executing binary, used to locate tools.
constexpr std::string_view proc_self_exe = "/proc/self/exe";

// Tool executable names as they appear on the system or in next to the editor.
constexpr std::string_view tool_console_name = "3dmppc";
constexpr std::string_view tool_burner_name = "mppcburner";
constexpr std::string_view tool_baker_name = "mppcbaker";
constexpr std::string_view tool_player_name = "player";

// Tool key names used in settings.toml and configuration.
constexpr std::string_view tool_console_key = "console";
constexpr std::string_view tool_burner_key = "burner";
constexpr std::string_view tool_baker_key = "baker";
constexpr std::string_view tool_player_key = "player";

// Command-line arguments for tool version checking.
constexpr std::string_view tool_version_arg = "--version";

// One table to search for tools by settings key and check them: key, executable name,
// whether to ask for version (only burner and baker), whether found next to editor.
struct tool_spec {
    std::string_view key;
    std::string_view name;
    bool ask_version;   // Only the burner and the baker are asked for a version.
    bool beside_editor; // Console, burner, baker are searched next to the editor.
    rv_editor_tool rv_editor_toolchain::*member;
};

constexpr tool_spec tool_specs[] = {
    { tool_console_key, tool_console_name, false, true, &rv_editor_toolchain::console },
    { tool_burner_key, tool_burner_name, true, true, &rv_editor_toolchain::burner },
    { tool_baker_key, tool_baker_name, true, true, &rv_editor_toolchain::baker },
    { tool_player_key, tool_player_name, false, false, &rv_editor_toolchain::player },
};

// Find tool specification by key; return nullptr if not found.
const tool_spec *find_tool_spec(std::string_view key)
{
    for (const auto &spec : tool_specs) {
        if (spec.key == key) {
            return &spec;
        }
    }
    return nullptr;
}

// Settings file name and its section for tool configuration.
constexpr std::string_view settings_file = "settings.toml";
constexpr std::string_view settings_tools_section = "tools";
// Origin descriptions for tools loaded from different sources.
constexpr std::string_view tool_origin_settings = "settings.toml";
constexpr std::string_view tool_origin_next_to_editor = "next to the editor";

// Manifest file name used by the project.
constexpr std::string_view manifest_file = "disc.toml";
// Directory name used for editor's cache and state subdirectories.
constexpr std::string_view editor_dir_name = "3dmppc-editor";

// Disc section names for assets, textures and sounds.
constexpr std::string_view disc_section_textures = "textures";
constexpr std::string_view disc_section_sounds = "sounds";
constexpr std::string_view disc_section_assets = "assets";

// File extensions for automatic section detection.
constexpr std::string_view file_ext_png = ".png";
constexpr std::string_view file_ext_wav = ".wav";

// XDG environment variables and fallback paths for user directories per XDG Base Directory spec.
constexpr std::string_view xdg_cache_var = "XDG_CACHE_HOME";
constexpr std::string_view xdg_cache_fallback = ".cache";
constexpr std::string_view xdg_config_var = "XDG_CONFIG_HOME";
constexpr std::string_view xdg_config_fallback = ".config";
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
constexpr size_t process_read_buf_size = 4096;
constexpr size_t process_read_large_buf_size = 65536;
constexpr int process_version_poll_ms = 5;
constexpr int process_version_timeout_sec = 1;
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

std::filesystem::path rv_editor_self_dir()
{
    std::error_code ec;
    const std::filesystem::path self = std::filesystem::read_symlink(std::string(proc_self_exe), ec);
    return ec ? std::filesystem::path() : self.parent_path();
}

// Read [tools] section from settings and apply configurations to toolchain.
void read_tools_section(rv_editor_toolchain &tc, const rv_pdklib::rv_manifest_tree &tree)
{
    for (const auto &section : tree.sections) {
        if (section.name != settings_tools_section) {
            continue;
        }
        for (const auto &entry : section.entries) {
            const tool_spec *spec = find_tool_spec(entry.key);
            if (spec == nullptr) {
                continue;
            }
            if (entry.value.kind != rv_pdklib::rv_manifest_value_kind::string) {
                continue;
            }
            tc.*(spec->member) = { entry.value.str, std::string(tool_origin_settings), "", "" };
        }
    }
}

// Read and parse settings.toml, then configure toolchain from its [tools] section.
void read_tool_settings(rv_editor_toolchain &tc)
{
    const std::filesystem::path config = rv_editor_xdg_dir(xdg_config_var.data(), xdg_config_fallback.data());
    if (config.empty()) {
        return;
    }
    tc.settings_path = config / std::string(settings_file);
    std::ifstream in(tc.settings_path, std::ios::binary);
    if (!in) {
        return;
    }
    std::ostringstream text;
    text << in.rdbuf();
    rv_pdklib::rv_manifest_tree tree;
    if (rv_pdklib::rv_manifest_read_tree(text.str(), tc.settings_path.string(), tree, tc.settings_error) != 0) {
        return;
    }
    read_tools_section(tc, tree);
}

void rv_editor_tool_check(rv_editor_tool &tool, const char *name)
{
    std::error_code ec;
    const std::filesystem::file_status st = std::filesystem::status(tool.path, ec);
    if (tool.path.empty() || ec || !std::filesystem::exists(st)) {
        const std::string tool_name = name;
        const std::string tool_path = tool.path.string();
        if (tool.path.empty()) {
            tool.problem = rv_editor_text_format("project.tool_not_found", std::make_format_args(tool_name));
        } else {
            tool.problem = rv_editor_text_format("project.tool_not_found_at", std::make_format_args(tool_name, tool_path));
        }
        return;
    }
    if (!std::filesystem::is_regular_file(st) ||
        (st.permissions() & std::filesystem::perms::owner_exec) == std::filesystem::perms::none) {
        const std::string tool_path = tool.path.string();
        tool.problem = rv_editor_text_format("project.tool_not_executable", std::make_format_args(tool_path));
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
    if (proc.start({ tool.path.string(), std::string(tool_version_arg) }, {}, error) != RV_OK) {
        tool.version = error;
        return;
    }
    std::string out;
    std::string err;
    const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(process_version_timeout_sec);
    while (!proc.poll() && std::chrono::steady_clock::now() < until) {
        proc.read(out, err, process_read_buf_size);
        std::this_thread::sleep_for(std::chrono::milliseconds(process_version_poll_ms));
    }
    proc.read(out, err, process_read_buf_size);
    const rv_editor_process::rv_editor_exit exit = proc.exit_status();
    if (!exit.exited || exit.signal != 0 || exit.code != 0 || out.empty()) {
        tool.version = "no version: " + std::string(tool_version_arg) + " is not understood";
        return;
    }
    tool.version = out.substr(0, out.find('\n'));
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
    proc_->read(out_, err, process_read_large_buf_size);
    const bool ended = proc_->poll();
    if (!ended && std::chrono::steady_clock::now() < until_) {
        return false;
    }
    proc_->read(out_, err, process_read_large_buf_size);
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
    tc.console = { self.empty() ? "" : self / std::string(tool_console_name), std::string(tool_origin_next_to_editor), "", "" };
    tc.burner = { self.empty() ? "" : self / std::string(tool_burner_name), std::string(tool_origin_next_to_editor), "", "" };
    tc.baker = { self.empty() ? "" : self / std::string(tool_baker_name), std::string(tool_origin_next_to_editor), "", "" };

    read_tool_settings(tc);

    rv_editor_tool_check(tc.console, tool_console_name.data());
    rv_editor_tool_check(tc.burner, tool_burner_name.data());
    rv_editor_tool_check(tc.baker, tool_baker_name.data());
    rv_editor_tool_check(tc.player, tool_player_name.data());
    if (tc.player.path.empty()) {
        tc.player.problem = rv_editor_text("project.player_not_set_file_settings");
    }
    rv_editor_tool_version(tc.burner);
    rv_editor_tool_version(tc.baker);
    return tc;
}

rv_editor_tool rv_editor_tool_probe(const char *key, const std::filesystem::path &override_path)
{
    const std::string_view k = key;
    const tool_spec *spec = find_tool_spec(k);
    const bool has_exe = spec != nullptr && spec->beside_editor;
    const std::string_view exe_name = has_exe ? spec->name : tool_player_name;
    const std::filesystem::path self = rv_editor_self_dir();
    rv_editor_tool tool;
    if (!override_path.empty()) {
        tool = { override_path, std::string(tool_origin_settings), "", "" };
    } else if (has_exe && !self.empty()) {
        tool = { self / std::string(exe_name), std::string(tool_origin_next_to_editor), "", "" };
    }
    rv_editor_tool_check(tool, exe_name.data());
    if (tool.path.empty() && !has_exe) {
        tool.problem = rv_editor_text("project.player_not_set_name");
    }
    if (has_exe && spec->ask_version) {
        rv_editor_tool_version(tool);
    }
    return tool;
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
    if (rel.ends_with(std::string(file_ext_png))) {
        return disc_section_textures.data();
    }
    if (rel.ends_with(std::string(file_ext_wav))) {
        return disc_section_sounds.data();
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
