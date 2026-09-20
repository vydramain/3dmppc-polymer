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
#include "pdklib/rv_manifest/rv_manifest.hpp"
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

// What -o/--unpacked resolved to, decided once and asked by every later
// stage instead of each one re-testing which flag was given.
enum class rv_burner_destination_kind
{
    archive,
    directory,
};

struct rv_burner_destination
{
    rv_burner_destination_kind kind;
    fs::path path;
};

// Runs the burn phase for whichever medium the destination names, and prints
// its [4/4] step.
static int rv_burner_destination_burn(const rv_burner_destination &destination,
    const rv_pdklib::rv_manifest &manifest, const fs::path &disc_module, const archive_plan &plan,
    std::string &error)
{
    if (destination.kind == rv_burner_destination_kind::directory) {
        if (burn_directory(destination.path, manifest, disc_module, plan, error) != 0) {
            return 1;
        }
        rv_burner_print_step(4, "burn", destination.path.string() + " (unpacked)");
        return 0;
    }

    int64_t burned_size = 0;
    if (burn_archive(destination.path, manifest, disc_module, plan, burned_size, error) != 0) {
        return 1;
    }
    rv_burner_print_step(4, "burn",
        destination.path.filename().string() + " (" + rv_burner_human_size(burned_size) + ")");
    return 0;
}

// --- [1/4] manifest ---
//
// Loads and validates the disc's manifest, and prints step 1.
static int rv_burner_build_manifest(const fs::path &disc_dir, rv_pdklib::rv_manifest &manifest, std::string &error)
{
    std::string er;
    const fs::path manifest_path = disc_dir / "disc.toml";
    if (rv_pdklib::rv_manifest_load(manifest_path.string(), manifest, er)) {
        rv_burner_print_error("tmp error text" + er);
        return 1;
    }

    if (!rv_pdklib::rv_manifest_validate(manifest, error)) {
        rv_burner_print_error(manifest_path.string() + ": " + error);
        return 1;
    }

    rv_burner_print_step(1, "manifest", manifest.disc_id + " — " + manifest.disc_title);
    return 0;
}

// --- [2/4] compile ---
//
// Generates the CMake project, configures and compiles it, and prints step 2.
static int rv_burner_build_compile(const rv_burner_options &options, const rv_pdklib::rv_manifest &manifest,
    const fs::path &project_dir, const fs::path &binary_dir, const std::vector<std::string> &absolute_includes,
    const std::vector<std::string> &absolute_sources, std::size_t source_count, std::string &error)
{
    if (create_cmakelists(options, manifest, project_dir, absolute_includes, absolute_sources, error) != 0) {
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

    rv_burner_print_step(2, "compile", std::to_string(source_count) + " source(s) -> " + k_disc_module_name);
    return 0;
}

// --- [3/4] assets ---
//
// Plans, checks, compiles or copies scripts, bakes textures, and prints step 3.
static int rv_burner_build_assets(const rv_burner_options &options, rv_pdklib::rv_manifest &manifest,
    const fs::path &disc_dir, const fs::path &texture_dir, const fs::path &scripts_dir,
    const rv_burner_destination &destination, archive_plan &plan, std::string &error)
{
    if (plan_archive(manifest, disc_dir, texture_dir, scripts_dir, plan, error) != 0) {
        rv_burner_print_error(error);
        return 1;
    }

    // Scripts and the memory they run in travel together, and the plan is the
    // first place both are known: the manifest states the budget, the glob
    // decides whether any .lua actually exists. Bytecode burned onto a disc
    // whose manifest declares no [budget.pccl] is dead weight — the console
    // reads that manifest, finds no Lua machine, and the disc can never load
    // the very files it carries. That is a mistake to catch on the author's
    // desk, not a silent archive.
    if (plan.script_count > 0 && manifest.budget.pccl.script_memory_size <= 0) {
        rv_burner_print_error(std::to_string(plan.script_count) +
            " lua script(s) to compile, but [budget.pccl] script_memory_size is not stated. "
            "A disc that carries scripts must declare the memory its lua machine gets.");
        return 1;
    }

    // The mirror of the refusal above, and the case that hides best: the author
    // wrote [budget.pccl], put .lua files in the disc directory, and forgot the
    // [scripts] section that tells the burner to look for them. The glob then
    // matches nothing, the plan holds no scripts, and without this the disc
    // burns with a lua machine declared and not one script aboard.
    // Only one cause can reach here. A [scripts] glob that matches nothing is
    // already refused by plan_archive above, by name, so an empty plan past
    // that point means there was no [scripts] section to glob with at all.
    if (plan.script_count == 0 && manifest.budget.pccl.script_memory_size > 0) {
        rv_burner_print_error(
            "[budget.pccl] declares a lua machine, but there is no [scripts] section to "
            "put a script in it. State [scripts] sources, or drop [budget.pccl].");
        return 1;
    }

    // The named entry must be one of the scripts actually planned. Without this
    // the disc burns with a lua machine pointed at a name nothing on the medium
    // answers to, and the failure surfaces on a player's machine as a missing
    // asset instead of here as a typo.
    if (manifest.budget.pccl.script_memory_size > 0) {
        bool entry_planned = false;
        std::string planned;
        for (std::size_t i = plan.first_script; i < plan.first_script + plan.script_count; ++i) {
            if (!planned.empty()) {
                planned += ", ";
            }
            planned += plan.items[i].name;
            if (plan.items[i].name == manifest.budget.pccl.script_entry) {
                entry_planned = true;
            }
        }
        if (!entry_planned) {
            rv_burner_print_error("[budget.pccl] script_entry '" + manifest.budget.pccl.script_entry +
                "' is not among the compiled scripts (" + planned + ")");
            return 1;
        }
    }

    // Which of the two a script gets — left as .lua or turned into .luac — is
    // decided once here by the destination, in a switch rather than an
    // if/else chain, so a third destination kind added later fails to compile
    // instead of silently falling into one of these two. One failure check
    // below covers both functions, instead of one per branch.
    int scripts_status = 1;
    switch (destination.kind) {
    case rv_burner_destination_kind::directory:
        scripts_status = prepare_scripts(plan, manifest, disc_dir, error);
        break;
    case rv_burner_destination_kind::archive:
        scripts_status = compile_scripts(plan, disc_dir, error);
        break;
    default:
        // Unreachable while rv_burner_destination_kind has only these two
        // values — kept so the switch stays exhaustive under a compiler
        // warning and so a future third kind fails here, not silently.
        error = "unknown destination kind";
        break;
    }
    if (scripts_status != 0) {
        rv_burner_print_error(error);
        return 1;
    }

    if (bake_textures(options.baker, manifest, disc_dir, plan, error) != 0) {
        rv_burner_print_error(error);
        return 1;
    }

    rv_burner_print_step(3, "assets",
        std::to_string(plan.texture_count) + " png -> .mppctex, " +
            std::to_string(plan.script_count) +
            (destination.kind == rv_burner_destination_kind::directory ? " lua (uncompiled), " : " lua -> .luac, ") +
            std::to_string(plan.asset_count) + " copied");
    return 0;
}

// --- clean up ---
//
// The build tree is scratch space and goes away, unless the developer asked
// to keep it — in which case it is a readable CMake project they can run
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
        rv_burner_print_warning("could not remove the build directory '" + project_dir.string() + "'");
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
        rv_burner_print_error("'" + options.operand + "' is not a directory");
        return 1;
    }

    // --unpacked and -o pick the same slot in the pipeline (where the burned
    // result goes) but produce different mediums, so exactly one of them names
    // the output.
    if (options.output.empty() == options.unpacked.empty()) {
        rv_burner_print_error("build needs exactly one of -o/--output or -u/--unpacked");
        return 1;
    }
    // Resolved once: every later stage asks `destination` what to do, instead
    // of re-testing which flag was given.
    const std::string &destination_operand = options.unpacked.empty() ? options.output : options.unpacked;
    const rv_burner_destination destination{
        options.unpacked.empty() ? rv_burner_destination_kind::archive : rv_burner_destination_kind::directory,
        fs::absolute(fs::path(destination_operand), ec)
    };
    if (ec) {
        rv_burner_print_error("cannot resolve output path '" + destination_operand + "'");
        return 1;
    }

    rv_pdklib::rv_manifest manifest;
    if (rv_burner_build_manifest(disc_dir, manifest, error) != 0) {
        return 1;
    }

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

    if (rv_burner_build_compile(options, manifest, project_dir, binary_dir, absolute_includes,
            absolute_sources, sources.size(), error) != 0) {
        return 1;
    }

    // The phase above proved this file exists; the burn phase carries it.
    const fs::path disc_module = binary_dir / k_disc_module_name;

    archive_plan plan;
    if (rv_burner_build_assets(options, manifest, disc_dir, texture_dir, scripts_dir, destination,
            plan, error) != 0) {
        return 1;
    }

    // --- [4/4] burn ---

    if (rv_burner_destination_burn(destination, manifest, disc_module, plan, error) != 0) {
        rv_burner_print_error(error);
        return 1;
    }

    rv_burner_build_cleanup(project_dir, options.keep_build);
    return 0;
}
