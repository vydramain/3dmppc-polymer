#include "rv_burner_build_assets.hpp"

#include <cstddef>
#include <format>
#include <string>
#include <vector>

#include "pdk/rv_err.h"
#include "rv_burner_assets/rv_burner_bake.hpp"
#include "rv_burner_assets/rv_burner_bake_models.hpp"
#include "rv_burner_assets/rv_burner_compile_scripts.hpp"
#include "rv_burner_assets/rv_burner_plan.hpp"
#include "rv_burner_print.hpp"

namespace
{
// Build phases in order: the assets phase is step 3.
constexpr int k_step_assets = 3;
} // namespace

namespace fs = std::filesystem;

namespace rv_pdktools
{

static bool script_entry_is_planned(const archive_plan &plan, const std::string &entry_name)
{
    for (std::size_t i = plan.first_script; i < plan.first_script + plan.script_count; ++i) {
        if (plan.items[i].name == entry_name) {
            return true;
        }
    }
    return false;
}

static std::string planned_script_names(const archive_plan &plan)
{
    std::string result;
    for (std::size_t i = plan.first_script; i < plan.first_script + plan.script_count; ++i) {
        if (!result.empty()) {
            result += ", ";
        }
        result += plan.items[i].name;
    }
    return result;
}

// Plans, checks, compiles or copies scripts, bakes textures, and prints step 3.
int rv_burner_build_assets(const rv_burner_options &options,
    rv_pdklib::rv_manifest &manifest,
    const rv_burner_asset_paths &paths,
    const rv_burner_destination &destination,
    archive_plan &plan,
    std::string &error)
{
    const fs::path &disc_dir = paths.disc_dir;

    const int plan_err = plan_archive(manifest, disc_dir, paths.textures, paths.scripts, paths.sounds, plan, error);
    if (plan_err != RV_OK) {
        rv_burner_print_error(error);
        return plan_err;
    }

    const int models_plan_err = plan_models(disc_dir, paths.models, plan, error);
    if (models_plan_err != RV_OK) {
        rv_burner_print_error(error);
        return models_plan_err;
    }

    // Scripts and the memory they run in travel together, and the plan is the
    // first place both are known: the manifest states the budget, the glob
    // decides whether any .lua actually exists. Bytecode burned onto a disc
    // whose manifest declares no [budget.pccl] is dead weight - the console
    // reads that manifest, finds no Lua machine, and the disc can never load
    // the very files it carries. That is a mistake to catch on the author's
    // desk, not a silent archive.
    if (plan.script_count > 0 && manifest.budget.pccl.script_memory_size <= 0) {
        rv_burner_print_error(std::to_string(plan.script_count) +
            " lua script(s) to compile, but [budget.pccl] script_memory_size is not stated. "
            "A disc that carries scripts must declare the memory its lua machine gets.");
        return RV_ERR_INVAL;
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
        rv_burner_print_error("[budget.pccl] declares a lua machine, but there is no [scripts] section to "
                              "put a script in it. State [scripts] sources, or drop [budget.pccl].");
        return RV_ERR_INVAL;
    }

    // The named entry must be one of the scripts actually planned. Without this
    // the disc burns with a lua machine pointed at a name nothing on the medium
    // answers to, and the failure surfaces on a player's machine as a missing
    // asset instead of here as a typo.
    const bool entry_ok =
        manifest.budget.pccl.script_memory_size <= 0 || script_entry_is_planned(plan, manifest.budget.pccl.script_entry);
    if (!entry_ok) {
        rv_burner_print_error(std::format("[budget.pccl] script_entry '{}' is not among the compiled scripts ({})",
            manifest.budget.pccl.script_entry,
            planned_script_names(plan)));
        return RV_ERR_INVAL;
    }

    // Which of the two a script gets - left as .lua or turned into .luac - is
    // decided once here by the destination, in a switch rather than an
    // if/else chain. A third destination kind added later is not a compile
    // error: it lands in default: and fails at run time with its reason. One
    // failure check below covers both functions, instead of one per branch.
    int scripts_status = RV_ERR_INVAL;
    switch (destination.kind) {
    case rv_burner_destination_kind::directory:
        scripts_status = prepare_scripts(plan, manifest, disc_dir, error);
        break;
    case rv_burner_destination_kind::archive:
        scripts_status = compile_scripts(plan, disc_dir, error);
        break;
    default:
        // Unreachable while the enum has these two values; a new kind lands here and fails with this reason.
        error = "unknown destination kind";
        break;
    }
    if (scripts_status != RV_OK) {
        rv_burner_print_error(error);
        return scripts_status;
    }

    const int textures_err = bake_textures(options.baker, manifest, disc_dir, plan, error);
    if (textures_err != RV_OK) {
        rv_burner_print_error(error);
        return textures_err;
    }

    const int sounds_err = bake_sounds(options.baker, disc_dir, plan, error);
    if (sounds_err != RV_OK) {
        rv_burner_print_error(error);
        return sounds_err;
    }

    const int stream_err = bake_stream(options.baker, disc_dir, plan, error);
    if (stream_err != RV_OK) {
        rv_burner_print_error(error);
        return stream_err;
    }

    std::vector<model_texture> model_textures;
    const int models_err = bake_models(options.baker, disc_dir, plan, model_textures, error);
    if (models_err != RV_OK) {
        rv_burner_print_error(error);
        return models_err;
    }

    const int model_plan_err = plan_model_textures(model_textures, paths.textures, plan, error);
    if (model_plan_err != RV_OK) {
        rv_burner_print_error(error);
        return model_plan_err;
    }

    const int model_textures_err = bake_model_textures(options.baker, manifest, model_textures, plan, error);
    if (model_textures_err != RV_OK) {
        rv_burner_print_error(error);
        return model_textures_err;
    }

    const char *scripts_wording =
        destination.kind == rv_burner_destination_kind::directory ? "lua (uncompiled)" : "lua -> .luac";
    rv_burner_print_step(k_step_assets,
        "assets",
        std::format("{} png -> .mppctex, {} wav/mp3 -> .pcm, {} {}, {} copied",
            plan.texture_count + plan.model_texture_count,
            plan.sound_count + plan.stream_count,
            plan.script_count,
            scripts_wording,
            plan.asset_count));
    return RV_OK;
}

} // namespace rv_pdktools
