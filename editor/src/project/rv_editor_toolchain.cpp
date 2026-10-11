// The tools the editor drives: where each is found, whether it can be used, and its version.

#include "project/rv_editor_project.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#include "pdk/rv_err.h"
#include "pdklib/rv_manifest/rv_manifest_dialect.hpp"
#include "text/rv_editor_text.hpp"

namespace rv_editor
{

namespace
{

// Symlink path to the currently executing binary, used to locate tools.
constexpr std::string_view proc_self_exe = "/proc/self/exe";

// Tool executable names as they appear on the system or in next to the editor.
constexpr std::string_view tool_console_name = "3dmppc";
constexpr std::string_view tool_burner_name = "mppcburner";
constexpr std::string_view tool_player_name = "player";

// The bakers, one per asset kind: the burner runs each by this name from the bakers' directory.
constexpr std::string_view tool_texbaker_name = "mppctexbaker"; // PNG
constexpr std::string_view tool_musbaker_name = "mppcmusbaker"; // WAV
constexpr std::string_view tool_modbaker_name = "mppcmodbaker"; // .glb

// Tool key names used in settings.toml and configuration.
constexpr std::string_view tool_console_key = "console";
constexpr std::string_view tool_burner_key = "burner";
constexpr std::string_view tool_baker_key = "baker";
constexpr std::string_view tool_player_key = "player";

// One table to search for tools by settings key and check them: key, executable name,
// whether to ask for version (only the burner), whether found next to editor.
struct tool_spec {
    std::string_view key;
    std::string_view name;
    bool ask_version;   // Only the burner is asked for a version.
    bool beside_editor; // Console and burner are searched next to the editor; the baker is a directory.
    rv_editor_tool rv_editor_toolchain::*member;
};

constexpr tool_spec tool_specs[] = {
    { tool_console_key, tool_console_name, false, true, &rv_editor_toolchain::console },
    { tool_burner_key, tool_burner_name, true, true, &rv_editor_toolchain::burner },
    { tool_baker_key, std::string_view(), false, false, &rv_editor_toolchain::baker }, // a directory: no executable name
    { tool_player_key, tool_player_name, false, false, &rv_editor_toolchain::player },
};

// The bakers the directory record holds: each is looked for by name in that directory.
struct baker_spec {
    std::string_view name;
    rv_editor_tool rv_editor_toolchain::*member;
};

constexpr baker_spec baker_specs[] = {
    { tool_texbaker_name, &rv_editor_toolchain::texbaker },
    { tool_musbaker_name, &rv_editor_toolchain::musbaker },
    { tool_modbaker_name, &rv_editor_toolchain::modbaker },
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

// XDG config variable and fallback: the editor's config directory holds settings.toml.
constexpr std::string_view xdg_config_var = "XDG_CONFIG_HOME";
constexpr std::string_view xdg_config_fallback = ".config";

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
            tc.*(spec->member) = { entry.value.str, std::string(tool_origin_settings), "", "", {} };
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

// The bakers' directory as the burner finds it: a regular file means the directory of its symlink
// target; empty stays empty. A missing path is returned as it is, the bakers then report it.
// Any other failure sets ec and returns the path given.
std::filesystem::path rv_editor_bakers_dir(const std::filesystem::path &given, std::error_code &ec)
{
    if (given.empty()) {
        return given;
    }
    const std::filesystem::file_status st = std::filesystem::status(given, ec);
    if (ec == std::errc::no_such_file_or_directory) {
        ec.clear();
        return given;
    }
    if (ec || !std::filesystem::is_regular_file(st)) {
        return given;
    }
    const std::filesystem::path target = std::filesystem::canonical(given, ec);
    return ec ? given : target.parent_path();
}

// Sets the three bakers in the bakers' directory record tc.baker (empty path: the editor's own
// directory) and checks each. tc.baker.problem is the first problem found. Versions are left to the caller.
void rv_editor_bakers_fill(rv_editor_toolchain &tc)
{
    std::error_code ec;
    const std::filesystem::path given = tc.baker.path.empty() ? rv_editor_self_dir() : tc.baker.path;
    const std::filesystem::path dir = rv_editor_bakers_dir(given, ec);
    tc.baker.path = dir;
    tc.baker.problem.clear();
    for (const baker_spec &spec : baker_specs) {
        rv_editor_tool &tool = tc.*(spec.member);
        tool = { dir.empty() ? std::filesystem::path() : dir / std::string(spec.name), tc.baker.origin, "", "", {} };
        rv_editor_tool_check(tool, spec.name.data());
        if (tc.baker.problem.empty()) {
            tc.baker.problem = tool.problem;
        }
    }
    if (ec) {
        // The directory itself cannot be read: that is the problem, whatever the bakers say.
        const std::string given_path = given.string();
        const std::string reason = ec.message();
        tc.baker.problem = rv_editor_text_format("project.baker_dir_error", std::make_format_args(given_path, reason));
    }
}

// Starts the tool's version check. Its outcome goes to tool.version, as the synchronous check
// did: a refusal there, unless the tool has a problem already (then no version is asked).
void rv_editor_tool_version_begin(rv_editor_tool &tool)
{
    if (!tool.problem.empty()) {
        return;
    }
    std::string error;
    const int rc = rv_editor_tool_version_start(tool, error);
    if (rc != RV_OK) {
        tool.version = "no version: " + error;
    }
}

} // namespace

int rv_editor_tool_version_start(rv_editor_tool &tool, std::string &error)
{
    if (!tool.problem.empty()) {
        error = tool.problem;
        return RV_ERR_INVAL;
    }
    if (tool.path.empty()) {
        error = "no path to ask for a version";
        return RV_ERR_NOENT;
    }
    return tool.version_check.start(tool.path, error);
}

int rv_editor_tool_version_poll(rv_editor_tool &tool, std::string &error)
{
    const int rc = tool.version_check.poll(error);
    if (rc != RV_OK) {
        tool.version = "no version: " + error;
        return rc;
    }
    if (!tool.version_check.running()) {
        tool.version = tool.version_check.version();
    }
    return RV_OK;
}

bool rv_editor_bakers_checking(const rv_editor_toolchain &tc)
{
    for (const baker_spec &spec : baker_specs) {
        const rv_editor_tool &tool = tc.*(spec.member);
        if (tool.version_check.running()) {
            return true;
        }
    }
    return false;
}

std::string rv_editor_bakers_version_text(const rv_editor_toolchain &tc)
{
    std::string text;
    for (const baker_spec &spec : baker_specs) {
        const rv_editor_tool &tool = tc.*(spec.member);
        text += (text.empty() ? "" : "; ") + std::string(spec.name) + ": " + tool.version;
    }
    return text;
}

rv_editor_toolchain rv_editor_toolchain_find()
{
    rv_editor_toolchain tc;
    const std::filesystem::path self = rv_editor_self_dir();
    tc.console = { self.empty() ? "" : self / std::string(tool_console_name),
        std::string(tool_origin_next_to_editor),
        "",
        "",
        {} };
    tc.burner = { self.empty() ? "" : self / std::string(tool_burner_name),
        std::string(tool_origin_next_to_editor),
        "",
        "",
        {} };
    tc.baker = { "", std::string(tool_origin_next_to_editor), "", "", {} };

    read_tool_settings(tc);

    rv_editor_tool_check(tc.console, tool_console_name.data());
    rv_editor_tool_check(tc.burner, tool_burner_name.data());
    rv_editor_tool_check(tc.player, tool_player_name.data());
    rv_editor_bakers_fill(tc);
    if (tc.player.path.empty()) {
        tc.player.problem = rv_editor_text("project.player_not_set_file_settings");
    }
    rv_editor_tool_version_begin(tc.burner);
    rv_editor_tool_version_begin(tc.texbaker);
    rv_editor_tool_version_begin(tc.musbaker);
    rv_editor_tool_version_begin(tc.modbaker);
    return tc;
}

rv_editor_tool rv_editor_tool_probe(const char *key, const std::filesystem::path &override_path)
{
    const std::string_view k = key;
    if (k == tool_baker_key) {
        rv_editor_toolchain bakers;
        const std::string_view origin = override_path.empty() ? tool_origin_next_to_editor : tool_origin_settings;
        bakers.baker = { override_path, std::string(origin), "", "", {} };
        rv_editor_bakers_fill(bakers);
        return std::move(bakers.baker);
    }
    const tool_spec *spec = find_tool_spec(k);
    const bool has_exe = spec != nullptr && spec->beside_editor;
    const std::string_view exe_name = has_exe ? spec->name : tool_player_name;
    const std::filesystem::path self = rv_editor_self_dir();
    rv_editor_tool tool;
    if (!override_path.empty()) {
        tool = { override_path, std::string(tool_origin_settings), "", "", {} };
    } else if (has_exe && !self.empty()) {
        tool = { self / std::string(exe_name), std::string(tool_origin_next_to_editor), "", "", {} };
    }
    rv_editor_tool_check(tool, exe_name.data());
    if (tool.path.empty() && !has_exe) {
        tool.problem = rv_editor_text("project.player_not_set_name");
    }
    if (has_exe && spec->ask_version) {
        rv_editor_tool_version_begin(tool);
    }
    return tool;
}

} // namespace rv_editor
