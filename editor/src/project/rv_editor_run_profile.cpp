// Run profiles: read from and written to the project's own editor settings, and
// checked against what the console accepts before Run starts one.

#include "project/rv_editor_run_profile.hpp"

#include <algorithm>
#include <array>
#include <fstream>
#include <sstream>
#include <string_view>
#include <system_error>

#include "pdk/rv_err.h"

#include "pdklib/rv_manifest/rv_manifest_dialect.hpp"
#include "project/rv_editor_toml.hpp"
#include "text/rv_editor_text.hpp"

namespace rv_editor
{

namespace
{

// Run configuration directory (state directory in project root; value must match rv_editor_watch).
constexpr std::string_view run_config_dir = ".3dmppc-editor";
constexpr std::string_view run_config_file = "project.toml";

// Console command-line flags for run profile options (see src/rv_pboot/rv_pboot_args.cpp).
constexpr std::string_view console_flag_mute = "--mute";
constexpr std::string_view console_flag_paused = "--paused";
constexpr std::string_view console_flag_fixed_step = "--fixed-step";
constexpr std::string_view console_flag_scale = "--scale";
constexpr std::string_view console_flag_scale_short = "-s"; // short form of --scale (window magnification)

// File header comment written to project configuration file.
constexpr std::string_view run_config_header =
    "# Run profiles of 3dmppc-editor (Run > Run Configuration); the editor rewrites this file.\n\n";

// TOML keys for run profile string fields.
constexpr std::string_view key_runtime = "runtime";
constexpr std::string_view key_memcard = "memcard";
constexpr std::string_view key_cwd = "cwd";

// TOML keys for run profile boolean flags.
constexpr std::string_view key_mute = "mute";
constexpr std::string_view key_paused = "paused";
constexpr std::string_view key_fixed_step = "fixed_step";
constexpr std::string_view key_reload_on_save = "reload_on_save";

// TOML keys for run profile array fields.
constexpr std::string_view key_args = "args";
constexpr std::string_view key_env = "env";

// TOML section names and active profile key.
constexpr std::string_view section_run = "run";
constexpr std::string_view key_active = "active";
constexpr std::string_view section_profile_prefix = "profile.";

// TOML array formatting delimiters.
constexpr char array_start = '[';
constexpr std::string_view array_sep = ", ";
constexpr char array_end = ']';

// TOML section and line formatting.
constexpr char section_open = '[';
constexpr char section_close = ']';
constexpr std::string_view key_value_sep = " = ";
constexpr char line_end = '\n';

std::filesystem::path rv_editor_run_config_path(const std::filesystem::path &root)
{
    return root / run_config_dir / run_config_file;
}

std::string rv_editor_run_array(const std::vector<std::string> &items)
{
    std::string out;
    out += array_start;
    for (size_t i = 0; i < items.size(); ++i) {
        if (i > 0) {
            out += array_sep;
        }
        out += rv_editor_toml_quote(items[i]);
    }
    out += array_end;
    return out;
}

// A profile name is a section name: letters, digits, '-' and '_'.
bool rv_editor_run_name_ok(std::string_view name)
{
    return !name.empty() && std::all_of(name.begin(), name.end(), [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
    });
}

void rv_editor_run_read_entry(rv_editor_run_profile &p, const rv_pdklib::rv_manifest_tree_entry &e)
{
    using kind = rv_pdklib::rv_manifest_value_kind;
    const rv_pdklib::rv_manifest_mvalue &v = e.value;
    if (v.kind == kind::string) {
        std::string *field = nullptr;
        if (e.key == key_runtime) {
            field = &p.runtime;
        } else if (e.key == key_memcard) {
            field = &p.memcard;
        } else if (e.key == key_cwd) {
            field = &p.cwd;
        }
        if (field != nullptr) {
            *field = v.str;
        }
    } else if (v.kind == kind::integer) {
        bool *flag = nullptr;
        if (e.key == key_mute) {
            flag = &p.mute;
        } else if (e.key == key_paused) {
            flag = &p.paused;
        } else if (e.key == key_fixed_step) {
            flag = &p.fixed_step;
        } else if (e.key == key_reload_on_save) {
            flag = &p.reload_on_save;
        }
        if (flag != nullptr) {
            *flag = v.num != 0;
        }
    } else if (e.key == key_args) {
        p.args = v.arr;
    } else if (e.key == key_env) {
        p.env = v.arr;
    }
}

} // namespace

rv_editor_run_config rv_editor_run_config_load(const std::filesystem::path &root)
{
    rv_editor_run_config config;
    config.profiles.clear();
    std::ifstream in(rv_editor_run_config_path(root), std::ios::binary);
    if (!in) {
        config.profiles.emplace_back();
        return config;
    }
    std::ostringstream text;
    text << in.rdbuf();
    rv_pdklib::rv_manifest_tree tree;
    if (rv_pdklib::rv_manifest_read_tree(text.str(), rv_editor_run_config_path(root).string(), tree, config.error) != 0) {
        config.profiles.emplace_back();
        return config;
    }
    std::string active;
    for (const rv_pdklib::rv_manifest_tree_section &section : tree.sections) {
        if (section.name == section_run) {
            for (const auto &e : section.entries) {
                if (e.key == key_active && e.value.kind == rv_pdklib::rv_manifest_value_kind::string) {
                    active = e.value.str;
                }
            }
            continue;
        }
        if (!section.name.starts_with(section_profile_prefix)) {
            continue;
        }
        rv_editor_run_profile p;
        p.name = section.name.substr(section_profile_prefix.size());
        for (const auto &e : section.entries) {
            rv_editor_run_read_entry(p, e);
        }
        config.profiles.push_back(std::move(p));
    }
    if (config.profiles.empty()) {
        config.profiles.emplace_back();
    }
    for (size_t i = 0; i < config.profiles.size(); ++i) {
        if (config.profiles[i].name == active) {
            config.active = i;
        }
    }
    return config;
}

int rv_editor_run_config_save(const std::filesystem::path &root, const rv_editor_run_config &config, std::string &error)
{
    std::string t = std::string(run_config_header);
    t += section_open;
    t += section_run;
    t += section_close;
    t += line_end;
    t += key_active;
    t += key_value_sep;
    t += rv_editor_toml_quote(config.profiles[config.active].name);
    t += line_end;
    for (const rv_editor_run_profile &p : config.profiles) {
        t += line_end;
        t += section_open;
        t += section_profile_prefix;
        t += p.name;
        t += section_close;
        t += line_end;
        t += key_runtime;
        t += key_value_sep;
        t += rv_editor_toml_quote(p.runtime);
        t += line_end;
        t += key_memcard;
        t += key_value_sep;
        t += rv_editor_toml_quote(p.memcard);
        t += line_end;
        t += key_cwd;
        t += key_value_sep;
        t += rv_editor_toml_quote(p.cwd);
        t += line_end;
        t += key_mute;
        t += key_value_sep;
        t += std::to_string(p.mute ? 1 : 0);
        t += line_end;
        t += key_paused;
        t += key_value_sep;
        t += std::to_string(p.paused ? 1 : 0);
        t += line_end;
        t += key_fixed_step;
        t += key_value_sep;
        t += std::to_string(p.fixed_step ? 1 : 0);
        t += line_end;
        t += key_args;
        t += key_value_sep;
        t += rv_editor_run_array(p.args);
        t += line_end;
        t += key_env;
        t += key_value_sep;
        t += rv_editor_run_array(p.env);
        t += line_end;
        t += key_reload_on_save;
        t += key_value_sep;
        t += std::to_string(p.reload_on_save ? 1 : 0);
        t += line_end;
    }
    const int code = rv_editor_file_replace(rv_editor_run_config_path(root), t, error);
    if (code != RV_OK) {
        return code;
    }
    return RV_OK;
}

std::filesystem::path rv_editor_run_profile_path(const std::string &path, const std::filesystem::path &root)
{
    if (path.empty()) {
        return {};
    }
    const std::filesystem::path p(path);
    return p.is_absolute() ? p : root / p;
}

std::string rv_editor_run_profile_problem(const rv_editor_run_profile &p, const std::filesystem::path &root)
{
    if (!rv_editor_run_name_ok(p.name)) {
        return rv_editor_text("run_profile.profile_name_invalid");
    }
    std::error_code ec;
    const std::filesystem::path runtime = rv_editor_run_profile_path(p.runtime, root);
    if (!runtime.empty() && !std::filesystem::is_regular_file(runtime, ec)) {
        const auto path_str = runtime.string();
        return rv_editor_text_format("run_profile.runtime_no_file", std::make_format_args(path_str));
    }
    const std::filesystem::path card = rv_editor_run_profile_path(p.memcard, root);
    if (!card.empty() && !std::filesystem::is_directory(card.parent_path(), ec)) {
        const auto path_str = card.parent_path().string();
        return rv_editor_text_format("run_profile.memcard_no_directory", std::make_format_args(path_str));
    }
    const std::filesystem::path cwd = rv_editor_run_profile_path(p.cwd, root);
    if (!cwd.empty() && !std::filesystem::is_directory(cwd, ec)) {
        const auto path_str = cwd.string();
        return rv_editor_text_format("run_profile.cwd_no_directory", std::make_format_args(path_str));
    }
    for (const std::string &e : p.env) {
        if (e.find('=') == std::string::npos || e.front() == '=') {
            return rv_editor_text_format("run_profile.env_invalid_format", std::make_format_args(e));
        }
    }
    // What the editor or a field of the form already decides.
    constexpr std::array<std::pair<std::string_view, std::string_view>, 12> owned = { {
        { "--dev", "run_profile.owned_dev" },
        { "--frame-fd", "run_profile.owned_frame_fd" },
        { "-m", "run_profile.owned_memcard" },
        { "--memcard", "run_profile.owned_memcard" },
        { "-M", "run_profile.owned_mute" },
        { console_flag_mute, "run_profile.owned_mute" },
        { console_flag_paused, "run_profile.owned_paused" },
        { "-F", "run_profile.owned_fixed_step" },
        { console_flag_fixed_step, "run_profile.owned_fixed_step" },
        { "-d", "run_profile.owned_disc" },
        { "--disc", "run_profile.owned_disc" },
        { console_flag_scale, "run_profile.owned_scale" },
    } };
    for (const std::string &a : p.args) {
        const std::string_view flag = std::string_view(a).substr(0, a.find('='));
        for (const auto &[name, why] : owned) {
            if (flag == name) {
                const char *why_text = rv_editor_text(why);
                return rv_editor_text_format("run_profile.console_option_owned", std::make_format_args(a, why_text));
            }
        }
        if (flag == console_flag_scale_short) {
            return rv_editor_text("run_profile.console_option_s");
        }
    }
    return {};
}

std::vector<std::string> rv_editor_run_profile_args(const rv_editor_run_profile &p)
{
    std::vector<std::string> args;
    if (p.mute) {
        args.push_back(std::string(console_flag_mute));
    }
    if (p.paused) {
        args.push_back(std::string(console_flag_paused));
    }
    if (p.fixed_step) {
        args.push_back(std::string(console_flag_fixed_step));
    }
    args.insert(args.end(), p.args.begin(), p.args.end());
    return args;
}

} // namespace rv_editor
