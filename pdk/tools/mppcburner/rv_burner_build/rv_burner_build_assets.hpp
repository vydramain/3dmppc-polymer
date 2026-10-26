#pragma once

#include <filesystem>
#include <string>

#include "pdklib/rv_manifest/rv_manifest.hpp"
#include "rv_burner_assets/rv_burner_plan.hpp"
#include "rv_burner_options.hpp"

namespace rv_pdktools
{

// What -o/--unpacked resolved to, decided once and asked by every later
// stage instead of each one re-testing which flag was given.
enum class rv_burner_destination_kind {
    archive,
    directory,
};

struct rv_burner_destination {
    rv_burner_destination_kind kind;
    std::filesystem::path path;
};

// The disc directory and the build tree's per-kind directories, decided once by
// the caller so the assets phase does not re-derive them.
struct rv_burner_asset_paths {
    std::filesystem::path disc_dir;
    std::filesystem::path textures;
    std::filesystem::path scripts;
    std::filesystem::path sounds;
    std::filesystem::path models;
};

/// Plan, check, compile or copy scripts, and bake textures, sounds and models.
///
/// @param options      the subcommand options; `baker` is read
/// @param manifest     the validated manifest; `budget.pccl` is read, and the manifest goes on to the plan and the bakers
/// @param paths        the disc directory and the build tree's asset directories
/// @param destination  decides whether scripts stay .lua or become .luac
/// @param plan         receives the planned archive
/// @param error        set with the refusal
/// @return RV_OK on success; else the RV_ERR_* of the refusing step, `error` set
int rv_burner_build_assets(const rv_burner_options &options,
    rv_pdklib::rv_manifest &manifest,
    const rv_burner_asset_paths &paths,
    const rv_burner_destination &destination,
    archive_plan &plan,
    std::string &error);

} // namespace rv_pdktools
