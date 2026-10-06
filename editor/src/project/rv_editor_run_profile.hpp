#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

namespace rv_editor
{

// Default run profile name, written to the profiles file.
constexpr std::string_view default_run_profile_name = "Default";

// How Run starts the development console. Tool paths are Settings'; a
// profile only says what differs for this project. Relative paths are the root's.
struct rv_editor_run_profile
{
    std::string name = std::string(default_run_profile_name);
    std::string runtime;           // empty: Settings' runtime
    std::string memcard;           // empty: the project's card in the state directory
    std::string cwd;               // empty: the project root
    bool mute = false;             // --mute
    bool paused = false;           // --paused: stopped before frame 0
    bool fixed_step = false;       // --fixed-step: no real-time wait, no audio
    std::vector<std::string> args; // more console options, one per entry, before the disc
    std::vector<std::string> env;  // KEY=VALUE over the editor's environment
    bool reload_on_save = false;   // a saved .lua file reloads a running entry script
};

// The profiles of <root>/.3dmppc-editor/project.toml; one Default when
// the file is missing or unreadable.
struct rv_editor_run_config
{
    std::vector<rv_editor_run_profile> profiles{ rv_editor_run_profile{} }; // never empty
    size_t active = 0;
    std::string error; // the file exists and does not read: the profiles are defaults
};

rv_editor_run_config rv_editor_run_config_load(const std::filesystem::path &root);
// Written through a temporary file and a rename. RV_OK or RV_ERR_IO with the reason.
int rv_editor_run_config_save(const std::filesystem::path &root, const rv_editor_run_config &config,
    std::string &error);

// Why the profile cannot start a console, known before it runs; empty
// when nothing stops it.
std::string rv_editor_run_profile_problem(const rv_editor_run_profile &p, const std::filesystem::path &root);

// A profile path as the console gets it: empty stays empty, relative joins the root.
std::filesystem::path rv_editor_run_profile_path(const std::string &path, const std::filesystem::path &root);

// The console options the profile adds before the disc.
std::vector<std::string> rv_editor_run_profile_args(const rv_editor_run_profile &p);

} // namespace rv_editor
