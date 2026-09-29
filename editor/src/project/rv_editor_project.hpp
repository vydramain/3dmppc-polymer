#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace rv_editor
{

// Where the editor keeps what is not the game's.
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
//   player = "/path/to/3dmppc"   (built without devtools; no default place)
struct rv_editor_toolchain
{
    rv_editor_tool console;
    rv_editor_tool burner;
    rv_editor_tool baker;
    rv_editor_tool player;
    std::filesystem::path settings_path;
    std::string settings_error; // settings.toml exists and does not parse
};

// Looks the tools up again and asks the burner and the baker for their version,
// so a replaced executable drops what was known about the old one (NFR-06).
rv_editor_toolchain rv_editor_toolchain_find();

// Settings' Check: the tool `key` ("console", "burner", "baker", "player") at
// `override_path`, or where it is found automatically when that is empty.
rv_editor_tool rv_editor_tool_probe(const char *key, const std::filesystem::path &override_path);

// The sources' version for a candidate record (REL-01): "git <commit>", then
// " + uncommitted changes" when the project's files differ from it; else why unknown.
std::string rv_editor_source_revision(const std::filesystem::path &root);

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
    // [budget.pccv] screen size: the proportions a starting Game tile takes.
    int64_t screen_w = 320;
    int64_t screen_h = 240;
    std::filesystem::path cache_dir; // builds and logs: $XDG_CACHE_HOME/3dmppc-editor/<hash>
    std::filesystem::path state_dir; // memory card: $XDG_STATE_HOME/3dmppc-editor/<hash>
    std::vector<std::string> assets_patterns;   // [assets] files, relative to root
    std::vector<std::string> textures_patterns; // [textures] files, relative to root
    std::vector<std::string> sounds_patterns;   // [sounds] files, relative to root
    bool has_build_section = false;             // disc.toml has [build] sources: a C++ disc
};

// True when `rel` (relative to p.root, '/'-separated) matches a pattern in
// any of the three sections above.
bool rv_editor_project_on_disc(const rv_editor_project &project, std::string_view rel);

// The disc.toml section a project file belongs to, by extension.
const char *rv_editor_project_disc_section(std::string_view rel);

// Adds `rel` itself as a pattern to its section and reloads the manifest.
// False with the reason on failure; disc.toml is untouched on failure.
bool rv_editor_project_put_on_disc(rv_editor_project &project, std::string_view rel, std::string &error);

// Opens `target`, a directory or its disc.toml. False with the reason when it
// is neither a directory with disc.toml nor a disc.toml.
bool rv_editor_project_open(const std::filesystem::path &target, rv_editor_project &project, std::string &error);

// Re-reads disc.toml after it changed on disk.
void rv_editor_project_reload_manifest(rv_editor_project &project);

} // namespace rv_editor
