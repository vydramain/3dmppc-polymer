// The tools the editor drives: where each is found, whether it can be used, and its version.

#include "project/rv_editor_project.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>

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
constexpr std::string_view tool_baker_name = "mppctexbaker";
constexpr std::string_view tool_player_name = "player";

// Tool key names used in settings.toml and configuration.
constexpr std::string_view tool_console_key = "console";
constexpr std::string_view tool_burner_key = "burner";
constexpr std::string_view tool_baker_key = "baker";
constexpr std::string_view tool_player_key = "player";

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
    tc.baker = { self.empty() ? "" : self / std::string(tool_baker_name), std::string(tool_origin_next_to_editor), "", "", {} };

    read_tool_settings(tc);

    rv_editor_tool_check(tc.console, tool_console_name.data());
    rv_editor_tool_check(tc.burner, tool_burner_name.data());
    rv_editor_tool_check(tc.baker, tool_baker_name.data());
    rv_editor_tool_check(tc.player, tool_player_name.data());
    if (tc.player.path.empty()) {
        tc.player.problem = rv_editor_text("project.player_not_set_file_settings");
    }
    rv_editor_tool_version_begin(tc.burner);
    rv_editor_tool_version_begin(tc.baker);
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
