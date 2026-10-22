#pragma once

#include "platform/rv_editor_process.hpp"
#include "project/rv_editor_version_job.hpp"

#include <chrono>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace rv_editor
{

// Console's native screen dimensions: the default proportions for a new project's Game tile
constexpr int64_t console_native_screen_width = 320;
constexpr int64_t console_native_screen_height = 240;

// True when `name` is a model file by pdklib's asset table (.gltf, .glb).
bool rv_editor_is_model_file_name(std::string_view name);

// Where the editor keeps what is not the game's.
// `var` is an XDG variable; without it, or when it is not absolute,
// $HOME/<home_fallback>. Empty when neither gives an absolute directory.
std::filesystem::path rv_editor_xdg_dir(const char *var, const char *home_fallback);

// One tool the editor drives as a process.
struct rv_editor_tool {
    std::filesystem::path path;
    std::string origin;  // "settings.toml" or "next to the editor"
    std::string problem; // why it cannot be used; empty when it can
    std::string version; // its `--version` line, for the diagnostics; empty while version_check runs
    // Background `--version` check, started by rv_editor_tool_version_start; running() until it ends.
    rv_editor_version_job version_check;
};

// The console and the authoring tools. Each is looked for next to the editor's
// own executable (a development build puts all of them in pconsole/) unless
// settings.toml names it:
//
//   [tools]
//   console = "/path/to/3dmppc"
//   burner = "/path/to/mppcburner"
//   baker = "/path/to/bakers"    (a directory, or a file in it; empty: next to the editor)
//   player = "/path/to/3dmppc"   (built without devtools; no default place)
//
// baker is the directory record of the bakers: path is the directory, problem the first problem
// of the three bakers or none, or that the directory cannot be read (project.baker_dir_error); it has
// no version. texbaker, musbaker, modbaker are the bakers the burner runs from that directory.
struct rv_editor_toolchain {
    rv_editor_tool console;
    rv_editor_tool burner;
    rv_editor_tool baker;
    rv_editor_tool texbaker; // PNG
    rv_editor_tool musbaker; // WAV
    rv_editor_tool modbaker; // .glb
    rv_editor_tool player;
    // The console a player is burnt with: firmware/3dmppc beside the burner, which takes it from there.
    rv_editor_tool firmware;
    std::filesystem::path settings_path;
    std::string settings_error; // settings.toml exists and does not parse
};

// The tools the editor lists and counts, in order: the console, the burner and the three bakers.
inline constexpr rv_editor_tool rv_editor_toolchain::*rv_editor_toolchain_tools[] = {
    &rv_editor_toolchain::console,
    &rv_editor_toolchain::burner,
    &rv_editor_toolchain::texbaker,
    &rv_editor_toolchain::musbaker,
    &rv_editor_toolchain::modbaker,
};

// Looks the tools up again and starts the burner's and the three bakers' version checks,
// so a replaced executable drops what was known about the old one.
rv_editor_toolchain rv_editor_toolchain_find();

// True while a version check of one of the three bakers runs.
bool rv_editor_bakers_checking(const rv_editor_toolchain &tc);

// The bakers' versions as "texbaker: <version>; musbaker: <version>; modbaker: <version>".
std::string rv_editor_bakers_version_text(const rv_editor_toolchain &tc);

// Settings' Check: the tool `key` ("console", "burner", "baker", "player") at
// `override_path`, or where it is found automatically when that is empty.
// Starts the burner's version check, as rv_editor_toolchain_find does; the baker asks for no version.
rv_editor_tool rv_editor_tool_probe(const char *key, const std::filesystem::path &override_path);

// Starts the tool's version check in the background, never waiting for it. RV_ERR_INVAL with
// tool.problem as the reason when the tool cannot be used, RV_ERR_NOENT when it has no path;
// otherwise the check's own code. Writes nothing to tool.
int rv_editor_tool_version_start(rv_editor_tool &tool, std::string &error);

// One step of a check started by rv_editor_tool_version_start; call while version_check.running().
// RV_OK while it runs and when it ends with tool.version set. When it ends without a version,
// tool.version becomes "no version: <reason>" and the code is returned with the reason in error.
int rv_editor_tool_version_poll(rv_editor_tool &tool, std::string &error);

// The sources' version for a candidate record: "git <commit>", then
// " + uncommitted changes" when the project's files differ from it; else why unknown.
// Runs git as child processes, each given 3 s; poll() once a frame, never blocks.
class rv_editor_revision_job
{
public:
    void start(const std::filesystem::path &root);
    // True when no lookup runs (text() is final). Advances a running one.
    bool poll();
    const std::string &text() const
    {
        return text_;
    }

private:
    // Starts a child process with argv in root directory. Returns RV_OK on success,
    // RV_ERR_IO when it cannot start.
    int launch(const std::vector<std::string> &argv);

    std::filesystem::path root_;
    std::filesystem::path git_;
    std::unique_ptr<rv_editor_process> proc_;
    std::string out_;
    std::string head_;
    std::string text_;
    std::chrono::steady_clock::time_point until_{};
    bool status_step_ = false;
};

// Which SDK the disc is built on, from disc.toml [build] sdk.
enum class rv_editor_sdk_state {
    pdkkit,  // sdk lists the "kit" tree
    bare,    // no [build] or no sdk key, or sdk without "kit"
    unknown, // disc.toml could not be loaded or failed validation; not reported as bare
};

// The game directory the window works on: a directory holding
// disc.toml, whichever of the two was opened.
struct rv_editor_project {
    bool open = false;
    std::filesystem::path root;     // canonical
    std::filesystem::path manifest; // root / "disc.toml"
    std::string disc_id;
    std::string disc_title;
    std::string manifest_error; // disc.toml does not parse; the project still opens
    // [budget.pccv] screen size: the proportions a starting Game tile takes.
    int64_t screen_w = console_native_screen_width;
    int64_t screen_h = console_native_screen_height;
    std::filesystem::path cache_dir;            // builds and logs: $XDG_CACHE_HOME/3dmppc-editor/<hash>
    std::filesystem::path state_dir;            // memory card: $XDG_STATE_HOME/3dmppc-editor/<hash>
    std::vector<std::string> assets_patterns;   // every [assets] pattern, of any kind, relative to root
    std::vector<std::string> textures_patterns; // never filled since the old [textures] section is no longer read; empty
    std::vector<std::string> sounds_patterns;   // never filled since the old [sounds] section is no longer read; empty
    // The [assets] patterns of the model kind (.gltf, .glb), relative to root. The import folder
    // is named only by a "<folder>/*.gltf" pattern (rv_editor_model_target_dir). An [assets] pattern like
    // "assets/models/*" is not in this list: the walk still finds its models by their own kind, but it names no folder.
    std::vector<std::string> models_patterns;
    bool has_build_section = false;             // disc.toml has [build] sources: a C++ disc
    rv_editor_sdk_state sdk = rv_editor_sdk_state::unknown; // pdkkit or bare; unknown until the manifest loads
};

// True when `rel` (relative to p.root, '/'-separated) matches a pattern in
// any of the four lists above.
bool rv_editor_project_on_disc(const rv_editor_project &project, std::string_view rel);

// The disc.toml section Add to disc writes any file to, whatever its extension: the burner sorts
// [assets] files by extension, so one section serves them all. The preview's tooltip names it too.
inline constexpr std::string_view rv_editor_disc_assets_section = "assets";

// Adds `rel` itself as a pattern to [assets] and reloads the manifest.
// RV_OK on success, error code from rv_editor_manifest_add_pattern on failure. disc.toml is untouched on failure.
int rv_editor_project_put_on_disc(rv_editor_project &project, std::string_view rel, std::string &error);

// Opens `target`, a directory or its disc.toml. RV_OK on success; RV_ERR_NOENT when the directory
// or disc.toml does not exist, RV_ERR_INVAL when target is a file but not disc.toml.
int rv_editor_project_open(const std::filesystem::path &target, rv_editor_project &project, std::string &error);

// Re-reads disc.toml after it changed on disk.
void rv_editor_project_reload_manifest(rv_editor_project &project);

} // namespace rv_editor
