#pragma once

#include <filesystem>
#include <string>

namespace rv_editor
{

// Where the editor keeps what is not the game's (docs/adr/0010-settings-storage.md).
// `var` is an XDG variable; without it, or when it is not absolute,
// $HOME/<home_fallback>. Empty when neither gives an absolute directory.
std::filesystem::path rv_editor_xdg_dir(const char *var, const char *home_fallback);

// One tool the editor drives as a process.
struct rv_editor_tool
{
    std::filesystem::path path;
    std::string origin;  // "settings.toml" or "next to the editor"
    std::string problem; // why it cannot be used; empty when it can
    std::string version; // its `--version` line, for the diagnostics (NFR-06)
};

// The console and the authoring tools. Each is looked for next to the editor's
// own executable (a development build puts all four in pconsole/) unless
// settings.toml names it:
//
//   [tools]
//   console = "/path/to/3dmppc"
//   burner = "/path/to/mppcburner"
//   baker = "/path/to/mppcbaker"
struct rv_editor_toolchain
{
    rv_editor_tool console;
    rv_editor_tool burner;
    rv_editor_tool baker;
    std::filesystem::path settings_path;
    std::string settings_error; // settings.toml exists and does not parse
};

// Looks the tools up again and asks the burner and the baker for their version,
// so a replaced executable drops what was known about the old one (NFR-06).
rv_editor_toolchain rv_editor_toolchain_find();

// The game directory the window works on (PRJ-01): a directory holding
// disc.toml, whichever of the two was opened.
struct rv_editor_project
{
    bool open = false;
    std::filesystem::path root;     // canonical
    std::filesystem::path manifest; // root / "disc.toml"
    std::string disc_id;
    std::string disc_title;
    std::string manifest_error;     // disc.toml does not parse; the project still opens
    std::filesystem::path cache_dir; // builds and logs: $XDG_CACHE_HOME/3dmppc-editor/<hash>
    std::filesystem::path state_dir; // memory card: $XDG_STATE_HOME/3dmppc-editor/<hash>
};

// Opens `target`, a directory or its disc.toml. False with the reason when it
// is neither a directory with disc.toml nor a disc.toml.
bool rv_editor_project_open(const std::filesystem::path &target, rv_editor_project &project, std::string &error);

// Re-reads disc.toml after it changed on disk.
void rv_editor_project_reload_manifest(rv_editor_project &project);

} // namespace rv_editor
