#include "rv_burner_build_runner.hpp"

#include <cstdint>
#include <expected>
#include <filesystem>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "rv_burner_assets/rv_burner_bake.hpp"
#include "rv_burner_assets/rv_burner_compile_scripts.hpp"
#include "rv_burner_assets/rv_burner_plan.hpp"
#include "rv_burner_burn/rv_burner_burn.hpp"
#include "rv_burner_common/rv_burner_globs.hpp"
#include "rv_burner_compile/rv_burner_check.hpp"
#include "rv_burner_compile/rv_burner_cmake.hpp"
#include "rv_burner_compile/rv_burner_compile_sources.hpp"
#include "rv_burner_manifest/rv_burner_manifest.hpp"
#include "rv_burner_print.hpp"

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

// The module compile_sources() produces, at the path the burn phase reads it
// from.
static constexpr const char *k_disc_module_name = "disc.so";

} // namespace rv_pdktools

int rv_pdktools::rv_burner_build_run(const rv_burner_options &options)
{
    std::error_code ec;
    std::string error;

    // --- the two paths the command line gave us ---

    const fs::path disc_dir = fs::weakly_canonical(fs::path(options.operand), ec);
    if (ec || !fs::is_directory(disc_dir, ec)) {
        rv_burner_print_error("'" + options.operand + "' is not a directory");
        return 1;
    }

    const fs::path output_path = fs::absolute(fs::path(options.output), ec);
    if (ec) {
        rv_burner_print_error("cannot resolve output path '" + options.output + "'");
        return 1;
    }

    // --- [1/4] manifest ---

    std::string er;
    rv_burner_manifest manifest;
    const fs::path manifest_path = disc_dir / "disc.toml";
    if (manifest_load(manifest_path.string(), manifest, er)) {
        rv_burner_print_error("tmp error text" + er);
        return 1;
    }

    if (!manifest_validate(manifest, error)) {
        rv_burner_print_error(manifest_path.string() + ": " + error);
        return 1;
    }

    rv_burner_print_step(1, "manifest", manifest.disc_id + " — " + manifest.disc_title);

    // --- the disc's own sources ---
    //
    // Expanded before anything is created, so a manifest that names no code is
    // refused before a build tree exists to clean up.
    std::vector<std::string> sources;
    if (!glob_expand(disc_dir, manifest.build_sources, sources, error)) {
        rv_burner_print_error("[build] sources: " + error);
        return 1;
    }

    if (sources.empty()) {
        rv_burner_print_error(
            "[build] sources is empty: a disc with no code cannot export an entry point");
        return 1;
    }

    std::vector<std::string> absolute_includes;
    std::vector<std::string> absolute_sources;
    if (check_sources_outside_disc(manifest, disc_dir, sources, absolute_includes,
            absolute_sources, error) != 0) {
        rv_burner_print_error("[build] include_dirs: " + error);
        return 1;
    }

    // --- the build tree ---

    const fs::path project_dir = options.build_dir.empty() ? disc_dir / k_default_build_dir_name : fs::absolute(options.build_dir, ec);
    const fs::path binary_dir = project_dir / k_binary_subdir;
    const fs::path scripts_dir = project_dir / k_scripts_subdir;
    const fs::path texture_dir = project_dir / k_textures_subdir;

    fs::create_directories(binary_dir, ec);
    fs::create_directories(scripts_dir, ec);
    fs::create_directories(texture_dir, ec);
    if (ec) {
        rv_burner_print_error("cannot create build directory '" + project_dir.string() + "'");
        return 1;
    }

    // --- [2/4] compile ---

    if (create_cmakelists(options, manifest, project_dir, absolute_includes, absolute_sources,
            error) != 0) {
        rv_burner_print_error(error);
        return 1;
    }

    if (configure_cmake(binary_dir, project_dir, error) != 0) {
        rv_burner_print_error(error);
        return 1;
    }

    if (compile_sources(options, binary_dir, error) != 0) {
        rv_burner_print_error(error);
        return 1;
    }

    rv_burner_print_step(2, "compile",
        std::to_string(sources.size()) + " source(s) -> " + k_disc_module_name);

    // The phase above proved this file exists; the burn phase carries it.
    const fs::path disc_module = binary_dir / k_disc_module_name;

    // --- [3/4] assets ---

    archive_plan plan;
    if (plan_archive(manifest, disc_dir, texture_dir, scripts_dir, plan, error) != 0) {
        rv_burner_print_error(error);
        return 1;
    }

    if (compile_scripts(plan, disc_dir, error) != 0) {
        rv_burner_print_error(error);
        return 1;
    }

    if (bake_textures(options.baker, manifest, disc_dir, plan, error) != 0) {
        rv_burner_print_error(error);
        return 1;
    }

    rv_burner_print_step(3, "assets",
        std::to_string(plan.texture_count) + " png -> .mppctex, " +
            std::to_string(plan.script_count) + " lua -> .luac, " +
            std::to_string(plan.asset_count) + " copied");

    // --- [4/4] burn ---

    int64_t burned_size = 0;
    if (burn_archive(output_path, manifest, disc_module, plan, burned_size, error) != 0) {
        rv_burner_print_error(error);
        return 1;
    }

    rv_burner_print_step(4, "burn",
        output_path.filename().string() + " (" + rv_burner_human_size(burned_size) + ")");

    // --- clean up ---
    //
    // The build tree is scratch space and goes away, unless the developer asked
    // to keep it — in which case it is a readable CMake project they can run
    // `ninja -v` in. It is also kept after a FAILURE, by every early return
    // above, for exactly that reason.
    if (!options.keep_build) {
        fs::remove_all(project_dir, ec);
        if (ec) {
            rv_burner_print_warning(
                "could not remove the build directory '" + project_dir.string() + "'");
        }
    }

    return 0;
}
