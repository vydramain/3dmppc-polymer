#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "rv_burner_assets/rv_burner_plan.hpp"
#include "pdklib/rv_manifest/rv_manifest.hpp"

namespace rv_pdktools
{

/// A texture a baked model names. It becomes an archive entry of its own.
struct model_texture {
    std::string name;          ///< "<stem>-<n>.mppctex", the flat name the model's part asks for
    std::string source;        ///< "<model source> texture <name>", for messages
    std::filesystem::path png; ///< "<stem>-<n>.png", beside the baked model
};

/// Bake every planned model to .mppcmod, refuse one the console would not parse, and collect the textures it names.
///
/// Each entry in the plan's model range is passed to mppcmodbaker, whose output lands
/// at that entry's `payload`. The baked file is read back whole and checked with
/// rv_mppcmod_int_parse, the same parser the console uses. The plan is only read.
///
/// @param baker_hint  value of `--baker` (a directory, or a file in one); empty searches for mppcmodbaker
/// @param disc_dir    absolute disc directory the sources are relative to
/// @param plan        the planned archive; only its model range is read
/// @param textures    receives the textures the models name, each name once; cleared first
/// @param error       set with the refusal, naming the baker or the baked file
/// @return RV_OK on success; else the RV_ERR_* of the refusing step, `error` set
int bake_models(const std::string &baker_hint,
    const std::filesystem::path &disc_dir,
    const archive_plan &plan,
    std::vector<model_texture> &textures,
    std::string &error);

/// Append the model textures to the plan as entries of their own, and refuse a plan whose names collide.
///
/// Planned before any texture is baked, so a model texture that shares a name with a
/// [textures] entry is refused with both sources named. The plan is changed only on success.
///
/// @param textures     what bake_models collected
/// @param texture_dir  directory baked .mppctex files are written to, the one [textures] uses
/// @param plan         the plan to extend; its model texture range is set
/// @param error        set with the refusal, naming the colliding entries
/// @return RV_OK on success; else the RV_ERR_* of the refusing step, `error` set
int plan_model_textures(const std::vector<model_texture> &textures,
    const std::filesystem::path &texture_dir,
    archive_plan &plan,
    std::string &error);

/// Bake each model texture from its PNG into the payload planned for it.
///
/// Textures are baked in planned order, each through bake_one_texture, so the format
/// is that of [assets.texture] and the size limit is [budget] budget.pccv.texture_max_width/height.
///
/// @param baker_hint  value of `--baker`; empty searches for mppctexbaker
/// @param manifest    the validated manifest; `assets_texture_format` and `budget` are read
/// @param textures    what bake_models collected
/// @param plan        the plan plan_model_textures extended
/// @param error       set with the refusal, naming the baker or the texture
/// @return RV_OK on success; else the RV_ERR_* of the refusing step, `error` set
int bake_model_textures(const std::string &baker_hint,
    const rv_pdklib::rv_manifest &manifest,
    const std::vector<model_texture> &textures,
    const archive_plan &plan,
    std::string &error);

} // namespace rv_pdktools
