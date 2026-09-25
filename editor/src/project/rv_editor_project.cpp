// The project a window works on, the tools it drives, and where its own files go.

#include "project/rv_editor_project.hpp"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <system_error>
#include <thread>

#include "pdklib/rv_manifest/rv_manifest.hpp"
#include "pdklib/rv_manifest/rv_manifest_dialect.hpp"
#include "platform/rv_editor_process.hpp"

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
        tool.problem = std::string(name) + " not found" + (tool.path.empty() ? "" : " at " + tool.path.string());
        return;
    }
    if (!std::filesystem::is_regular_file(st) ||
        (st.permissions() & std::filesystem::perms::owner_exec) == std::filesystem::perms::none) {
        tool.problem = tool.path.string() + " is not an executable file";
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
    if (!proc.start({ tool.path.string(), "--version" }, {}, error)) {
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
    rv_editor_tool_version(tc.burner);
    rv_editor_tool_version(tc.baker);
    return tc;
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
        return;
    }
    project.manifest_error.clear();
    project.disc_id = manifest.disc_id;
    project.disc_title = manifest.disc_title;
}

} // namespace rv_editor
