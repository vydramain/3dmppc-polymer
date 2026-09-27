// Run profiles: read from and written to the project's own editor settings, and
// checked against what the console accepts before Run starts one.

#include "project/rv_editor_run_profile.hpp"

#include <algorithm>
#include <array>
#include <fstream>
#include <sstream>
#include <string_view>
#include <system_error>

#include "pdklib/rv_manifest/rv_manifest_dialect.hpp"

namespace rv_editor
{

namespace
{

std::filesystem::path rv_editor_run_config_path(const std::filesystem::path &root)
{
    return root / ".3dmppc-editor" / "project.toml";
}

// The escapes the disc.toml lexer reads back, and no others.
std::string rv_editor_run_quote(std::string_view s)
{
    std::string out = "\"";
    for (const char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\t': out += "\\t"; break;
            case '\r': out += "\\r"; break;
            default: out += c; break;
        }
    }
    return out + "\"";
}

std::string rv_editor_run_array(const std::vector<std::string> &items)
{
    std::string out = "[";
    for (size_t i = 0; i < items.size(); ++i) {
        out += (i == 0 ? "" : ", ") + rv_editor_run_quote(items[i]);
    }
    return out + "]";
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
        std::string *field = e.key == "runtime" ? &p.runtime : e.key == "memcard" ? &p.memcard
            : e.key == "cwd"                                           ? &p.cwd
                                                                       : nullptr;
        if (field != nullptr) {
            *field = v.str;
        }
    } else if (v.kind == kind::integer) {
        bool *flag = e.key == "mute" ? &p.mute : e.key == "paused" ? &p.paused
            : e.key == "fixed_step"                                 ? &p.fixed_step
            : e.key == "reload_on_save"                             ? &p.reload_on_save
                                                                    : nullptr;
        if (flag != nullptr) {
            *flag = v.num != 0;
        }
    } else if (e.key == "args") {
        p.args = v.arr;
    } else if (e.key == "env") {
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
        if (section.name == "run") {
            for (const auto &e : section.entries) {
                if (e.key == "active" && e.value.kind == rv_pdklib::rv_manifest_value_kind::string) {
                    active = e.value.str;
                }
            }
            continue;
        }
        if (!section.name.starts_with("profile.")) {
            continue;
        }
        rv_editor_run_profile p;
        p.name = section.name.substr(8);
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

bool rv_editor_run_config_save(const std::filesystem::path &root, const rv_editor_run_config &config,
    std::string &error)
{
    std::string t = "# Run profiles of 3dmppc-editor (Run > Run Configuration); the editor rewrites this file.\n\n";
    t += "[run]\nactive = " + rv_editor_run_quote(config.profiles[config.active].name) + "\n";
    for (const rv_editor_run_profile &p : config.profiles) {
        t += "\n[profile." + p.name + "]\n";
        t += "runtime = " + rv_editor_run_quote(p.runtime) + "\n";
        t += "memcard = " + rv_editor_run_quote(p.memcard) + "\n";
        t += "cwd = " + rv_editor_run_quote(p.cwd) + "\n";
        t += "mute = " + std::to_string(p.mute ? 1 : 0) + "\n";
        t += "paused = " + std::to_string(p.paused ? 1 : 0) + "\n";
        t += "fixed_step = " + std::to_string(p.fixed_step ? 1 : 0) + "\n";
        t += "args = " + rv_editor_run_array(p.args) + "\n";
        t += "env = " + rv_editor_run_array(p.env) + "\n";
        t += "reload_on_save = " + std::to_string(p.reload_on_save ? 1 : 0) + "\n";
    }
    const std::filesystem::path path = rv_editor_run_config_path(root);
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    const std::filesystem::path tmp = path.string() + ".tmp";
    std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
    out << t;
    out.close();
    if (!out) {
        error = "cannot write " + tmp.string();
        return false;
    }
    std::filesystem::rename(tmp, path, ec);
    if (ec) {
        error = path.string() + ": " + ec.message();
        return false;
    }
    return true;
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
        return "The profile name takes letters, digits, '-' and '_' only";
    }
    std::error_code ec;
    const std::filesystem::path runtime = rv_editor_run_profile_path(p.runtime, root);
    if (!runtime.empty() && !std::filesystem::is_regular_file(runtime, ec)) {
        return "Runtime: no file at " + runtime.string();
    }
    const std::filesystem::path card = rv_editor_run_profile_path(p.memcard, root);
    if (!card.empty() && !std::filesystem::is_directory(card.parent_path(), ec)) {
        return "Memory card: no directory " + card.parent_path().string();
    }
    const std::filesystem::path cwd = rv_editor_run_profile_path(p.cwd, root);
    if (!cwd.empty() && !std::filesystem::is_directory(cwd, ec)) {
        return "Working directory: no directory " + cwd.string();
    }
    for (const std::string &e : p.env) {
        if (e.find('=') == std::string::npos || e.front() == '=') {
            return "Environment: \"" + e + "\" is not KEY=VALUE";
        }
    }
    // What the editor or a field of the form already decides.
    constexpr std::array<std::pair<std::string_view, std::string_view>, 12> owned = { {
        { "--dev", "the editor attaches the development channel itself" },
        { "--frame-fd", "the editor gives the frame descriptor itself" },
        { "-m", "use the Memory card field" },
        { "--memcard", "use the Memory card field" },
        { "-M", "use Mute" },
        { "--mute", "use Mute" },
        { "--paused", "use Start Paused" },
        { "-F", "use Fixed Step" },
        { "--fixed-step", "use Fixed Step" },
        { "-d", "the build gives the disc" },
        { "--disc", "the build gives the disc" },
        { "--scale", "the console opens no window here: View > Game Scale scales the Game view" },
    } };
    for (const std::string &a : p.args) {
        const std::string_view flag = std::string_view(a).substr(0, a.find('='));
        for (const auto &[name, why] : owned) {
            if (flag == name) {
                return "Console options: " + a + ": " + std::string(why);
            }
        }
        if (flag == "-s") {
            return "Console options: -s: the console opens no window here: View > Game Scale scales the Game view";
        }
    }
    return {};
}

std::vector<std::string> rv_editor_run_profile_args(const rv_editor_run_profile &p)
{
    std::vector<std::string> args;
    if (p.mute) {
        args.push_back("--mute");
    }
    if (p.paused) {
        args.push_back("--paused");
    }
    if (p.fixed_step) {
        args.push_back("--fixed-step");
    }
    args.insert(args.end(), p.args.begin(), p.args.end());
    return args;
}

} // namespace rv_editor
