#include "rv_burner_build_runner.hpp"

#include <cstdint>
#include <expected>
#include <filesystem>
#include <format>
#include <fstream>
#include <sstream>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "pdk/rv_err.h"
#include "rv_burner_build/rv_burner_build_assets.hpp"
#include "rv_burner_build/rv_burner_target.hpp"
#include "rv_burner_assets/rv_burner_plan.hpp"
#include "rv_burner_build/rv_burner_map.hpp"
#include "rv_burner_burn/rv_burner_burn.hpp"
#include "rv_burner_common/rv_burner_globs.hpp"
#include "rv_burner_compile/rv_burner_check.hpp"
#include "rv_burner_compile/rv_burner_cmake.hpp"
#include "rv_burner_compile/rv_burner_compile_sources.hpp"
#include "pdklib/rv_manifest/rv_manifest.hpp"
#include "rv_burner_print.hpp"

namespace
{
// Build phases in order: manifest, compile, assets, burn.
constexpr int k_step_manifest = 1;
constexpr int k_step_compile = 2;
constexpr int k_step_burn = 4;
} // namespace

namespace fs = std::filesystem;

namespace rv_pdktools
{

// The generated build tree, when --keep-build did not name one. Inside the disc
// directory so it is obvious what it belongs to, and dot-prefixed so it does not
// look like disc content.
static constexpr const char *k_default_build_dir_name = ".mppcburn";

// Where each phase leaves what it produces, all under the build tree.
static constexpr const char *k_binary_subdir = "build";
static constexpr const char *k_scripts_subdir = "scripts";
static constexpr const char *k_textures_subdir = "textures";
static constexpr const char *k_sounds_subdir = "sounds";
static constexpr const char *k_models_subdir = "models";

// The module compile_sources() produces, at the path the burn phase reads it
// from.
static constexpr const char *k_disc_module_name = "disc.so";

// Burns the disc into an unpacked directory and prints its [4/4] step.
static int rv_burner_burn_to_directory(const rv_burner_destination &destination,
    const rv_pdklib::rv_manifest &manifest,
    const fs::path &disc_module,
    const archive_plan &plan,
    std::string &error)
{
    const int burn_err = burn_directory(destination.path, manifest, disc_module, plan, error);
    if (burn_err != RV_OK) {
        return burn_err;
    }
    rv_burner_print_step(k_step_burn, "burn", destination.path.string() + " (unpacked)");
    return RV_OK;
}

// Burns the disc into one .mppcdisc archive and prints its [4/4] step.
static int rv_burner_burn_to_archive(const rv_burner_destination &destination,
    const rv_pdklib::rv_manifest &manifest,
    const fs::path &disc_module,
    const archive_plan &plan,
    std::string &error)
{
    int64_t burned_size = 0;
    const int burn_err = burn_archive(destination.path, manifest, disc_module, plan, burned_size, error);
    if (burn_err != RV_OK) {
        return burn_err;
    }
    rv_burner_print_step(k_step_burn,
        "burn",
        std::format("{} ({})", destination.path.filename().string(), rv_burner_human_size(burned_size)));
    return RV_OK;
}

// Runs the burn phase for whichever medium the destination names. No default:
// -Wswitch flags a new kind; the trailing return covers a value outside the enum.
static int rv_burner_destination_burn(const rv_burner_destination &destination,
    const rv_pdklib::rv_manifest &manifest,
    const fs::path &disc_module,
    const archive_plan &plan,
    std::string &error)
{
    switch (destination.kind) {
    case rv_burner_destination_kind::directory:
        return rv_burner_burn_to_directory(destination, manifest, disc_module, plan, error);
    case rv_burner_destination_kind::archive:
        return rv_burner_burn_to_archive(destination, manifest, disc_module, plan, error);
    }
    error = "unknown destination kind";
    return RV_ERR_INVAL;
}

// --- [1/4] manifest ---
//
// Loads and validates the disc's manifest, and prints step 1.
int rv_burner_build_manifest(const fs::path &disc_dir, rv_pdklib::rv_manifest &manifest, std::string &error)
{
    std::string er;
    const fs::path manifest_path = disc_dir / "disc.toml";
    const int load_err = rv_pdklib::rv_manifest_load(manifest_path.string(), manifest, er);
    if (load_err != RV_OK) {
        // The loader already stamps the path on every line it reports.
        rv_burner_print_error(er);
        return load_err;
    }

    const int validate_err = rv_pdklib::rv_manifest_validate(manifest, error);
    if (validate_err != RV_OK) {
        rv_burner_print_error(std::format("{}: {}", manifest_path.string(), error));
        return validate_err;
    }

    rv_burner_print_step(k_step_manifest, "manifest", std::format("{} — {}", manifest.disc_id, manifest.disc_title));
    return RV_OK;
}

// --- [2/4] compile ---
//
// Generates the CMake project, configures and compiles it, and prints step 2.
static int rv_burner_build_compile(const rv_burner_options &options,
    const rv_pdklib::rv_manifest &manifest,
    const fs::path &project_dir,
    const fs::path &binary_dir,
    const std::vector<std::string> &absolute_includes,
    const std::vector<std::string> &absolute_sources,
    std::size_t source_count,
    std::string &error)
{
    const int cmake_err = create_cmakelists(options, manifest, project_dir, absolute_includes, absolute_sources, error);
    if (cmake_err != RV_OK) {
        rv_burner_print_error(error);
        return cmake_err;
    }

    const int configure_err = configure_cmake(binary_dir, project_dir, error);
    if (configure_err != RV_OK) {
        rv_burner_print_error(error);
        return configure_err;
    }

    const int compile_err = compile_sources(options, binary_dir, error);
    if (compile_err != RV_OK) {
        rv_burner_print_error(error);
        return compile_err;
    }

    rv_burner_print_step(k_step_compile, "compile", std::format("{} source(s) -> {}", source_count, k_disc_module_name));
    return RV_OK;
}

// --- compile database for clangd ---
//
// configure_cmake() asked cmake to write compile_commands.json into binary_dir
// (the standard cmake mechanism). Read here, before cleanup deletes binary_dir,
// but written to disc_dir/.mppcburn only after cleanup runs: with a default
// build_dir that IS disc_dir/.mppcburn, cleanup would otherwise remove the
// copy along with the scratch tree it just left. Best-effort throughout: a
// disc's compile database is a convenience, never a reason to fail a build.
static bool rv_burner_read_compile_database(const fs::path &binary_dir, std::string &content)
{
    std::ifstream in(binary_dir / "compile_commands.json", std::ios::binary);
    if (!in) {
        return false;
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    content = buffer.str();
    return true;
}

static void rv_burner_write_compile_database(const fs::path &disc_dir, const std::string &content)
{
    std::error_code ec;
    const fs::path dest_dir = disc_dir / k_default_build_dir_name;
    fs::create_directories(dest_dir, ec);
    const fs::path dest = dest_dir / "compile_commands.json";
    const fs::path temp = dest_dir / "compile_commands.json.tmp";

    std::ofstream out(temp, std::ios::binary | std::ios::trunc);
    out.write(content.data(), static_cast<std::streamsize>(content.size()));
    bool ok = static_cast<bool>(out);
    out.close();
    if (ok) { // rename, not copy: a reader of dest never sees a half-written file
        fs::rename(temp, dest, ec);
        ok = !ec;
    }
    if (!ok) {
        fs::remove(temp, ec);
        rv_burner_print_warning(std::format("could not export the compile database to '{}'", dest.string()));
    }
}

// --- clean up ---
//
// The build tree is scratch space and goes away, unless the developer asked
// to keep it - in which case it is a readable CMake project they can run
// `ninja -v` in. It is also kept after a FAILURE, by every early return in
// rv_burner_build_run, for exactly that reason.
static void rv_burner_build_cleanup(const fs::path &project_dir, bool keep_build)
{
    if (keep_build) {
        return;
    }
    std::error_code ec;
    fs::remove_all(project_dir, ec);
    if (ec) {
        rv_burner_print_warning(std::format("could not remove the build directory '{}'", project_dir.string()));
    }
}

} // namespace rv_pdktools

int rv_pdktools::rv_burner_build_run(const rv_burner_options &options)
{
    std::error_code ec;
    std::string error;

    // --- the two paths the command line gave us ---

    const fs::path disc_dir = fs::weakly_canonical(fs::path(options.operand), ec);
    if (ec || !fs::is_directory(disc_dir, ec)) {
        rv_burner_print_error(std::format("'{}' is not a directory", options.operand));
        return RV_ERR_NOENT;
    }

    // Looked up before the manifest is read: an unknown name is refused with the names that exist.
    const rv_burner_target_spec *target = find_target(options.target);
    if (nullptr == target) {
        rv_burner_print_error(std::format("unknown target '{}'; expected one of: {}", options.target, target_names()));
        return RV_ERR_INVAL;
    }

    rv_pdklib::rv_manifest manifest;
    const int manifest_err = rv_burner_build_manifest(disc_dir, manifest, error);
    if (manifest_err != RV_OK) {
        return manifest_err;
    }

    // Resolved once: every later stage asks `destination` what to do, instead
    // of re-testing which flag was given.
    rv_burner_destination destination{};
    const int resolve_err = target->resolve(options, manifest, destination, error);
    if (resolve_err != RV_OK) {
        rv_burner_print_error(error);
        return resolve_err;
    }

    // burn_directory writes disc.so beside the disc, so a target without native code has nothing to put there.
    const bool no_native_directory = !target->needs_native_code && destination.kind == rv_burner_destination_kind::directory;
    if (no_native_directory) {
        rv_burner_print_error(
            std::format("--target {} has no native code, so it cannot write a directory: give -o PATH", target->name));
        return RV_ERR_INVAL;
    }

    // --- the disc's own sources ---
    //
    // Expanded before anything is created, so a manifest that names no code is
    // refused before a build tree exists to clean up.
    std::vector<std::string> sources;
    const int glob_err = glob_expand(disc_dir, manifest.build_sources, sources, error);
    if (glob_err != RV_OK) {
        rv_burner_print_error("[build] sources: " + error);
        return glob_err;
    }

    if (sources.empty()) {
        rv_burner_print_error("[build] sources is empty: a disc with no code cannot export an entry point");
        return RV_ERR_INVAL;
    }

    std::vector<std::string> absolute_includes;
    std::vector<std::string> absolute_sources;
    const int include_err = check_sources_outside_disc(manifest, disc_dir, sources, absolute_includes, absolute_sources, error);
    if (include_err != RV_OK) {
        rv_burner_print_error("[build] include_dirs: " + error);
        return include_err;
    }

    // --- the build tree ---

    const fs::path project_dir =
        options.build_dir.empty() ? disc_dir / k_default_build_dir_name : fs::absolute(options.build_dir, ec);
    if (ec) {
        rv_burner_print_error(std::format("cannot resolve build directory '{}'", options.build_dir));
        return RV_ERR_IO;
    }
    const fs::path binary_dir = project_dir / k_binary_subdir;
    const rv_burner_asset_paths asset_paths{ disc_dir,
        project_dir / k_textures_subdir,
        project_dir / k_scripts_subdir,
        project_dir / k_sounds_subdir,
        project_dir / k_models_subdir };

    const std::vector<fs::path> build_dirs{ binary_dir,
        asset_paths.scripts,
        asset_paths.textures,
        asset_paths.sounds,
        asset_paths.models };
    for (const auto &dir : build_dirs) {
        fs::create_directories(dir, ec);
        if (ec) {
            rv_burner_print_error(std::format("cannot create build directory '{}'", project_dir.string()));
            return RV_ERR_IO;
        }
    }

    if (target->needs_native_code) {
        const int compile_err = rv_burner_build_compile(options,
            manifest,
            project_dir,
            binary_dir,
            absolute_includes,
            absolute_sources,
            sources.size(),
            error);
        if (compile_err != RV_OK) {
            return compile_err;
        }
    } else {
        rv_burner_print_step(k_step_compile, "compile", "skipped: this target has no native code");
    }

    // Read now, while binary_dir still exists; written out only after cleanup. No compile, no database.
    std::string compile_database;
    const bool have_compile_database =
        target->needs_native_code && rv_burner_read_compile_database(binary_dir, compile_database);

    // The phase above proved this file exists; the burn phase carries it. Empty when there is no compile.
    const fs::path disc_module = target->needs_native_code ? binary_dir / k_disc_module_name : fs::path();

    archive_plan plan;
    const int assets_err = rv_burner_build_assets(options, manifest, asset_paths, destination, plan, error);
    if (assets_err != RV_OK) {
        return assets_err;
    }

    // --- [4/4] burn ---

    const int burn_err = rv_burner_destination_burn(destination, manifest, disc_module, plan, error);
    if (burn_err != RV_OK) {
        rv_burner_print_error(error);
        return burn_err;
    }
    if (nullptr != target->finish) {
        const int finish_err = target->finish(destination, error);
        if (finish_err != RV_OK) {
            rv_burner_print_error(error);
            return finish_err;
        }
    }
    // Only a build that succeeded replaces the map.
    const int map_err = options.map.empty() ? RV_OK : write_map(fs::absolute(options.map, ec), manifest, sources, plan, error);
    if (map_err != RV_OK) {
        rv_burner_print_error(error);
        return map_err;
    }

    rv_burner_build_cleanup(project_dir, options.keep_build);
    if (have_compile_database) {
        rv_burner_write_compile_database(disc_dir, compile_database);
    }
    return RV_OK;
}
