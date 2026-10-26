#pragma once

#include <cstddef>
#include <filesystem>
#include <string>

#include "rv_burner_common/rv_burner_archive.hpp"
#include "pdklib/rv_manifest/rv_manifest.hpp"

namespace rv_pdktools
{

// --- plan first, work second ---
//
// The whole archive is planned before anything is baked or compiled: every flat
// name, and the original it came from. That ordering is what makes the collision
// diagnostic useful. Baking first would mean two colliding PNGs are discovered
// as two identical paths to a .mppctex that had already overwritten itself, and
// the message could no longer name the files the author has to rename.

/// The planned archive, plus where each kind of entry sits inside it.
///
/// One vector rather than three because the burn phase writes it in order and
/// the collision check needs to see all of it at once. The ranges are how the
/// later stages find their own entries again: textures go to mppctexbaker, scripts
/// go to luajit, copied assets need neither.
struct archive_plan {
    std::vector<archive_item> items; ///< every entry, in archive order

    std::size_t asset_count = 0;         ///< copied verbatim; they lead the vector
    std::size_t first_texture = 0;       ///< index of the first texture entry
    std::size_t texture_count = 0;       ///< how many texture entries follow it
    std::size_t first_script = 0;        ///< index of the first script entry
    std::size_t script_count = 0;        ///< how many script entries follow it
    std::size_t first_sound = 0;         ///< index of the first sound entry
    std::size_t sound_count = 0;         ///< how many sound entries follow it
    std::size_t first_music = 0;         ///< index of the first music entry
    std::size_t music_count = 0;         ///< how many music entries follow it
    std::size_t first_model = 0;         ///< index of the first model entry
    std::size_t model_count = 0;         ///< how many model entries follow it
    std::size_t first_model_texture = 0; ///< index of the first texture a model names
    std::size_t model_texture_count = 0; ///< how many model texture entries follow it
    std::vector<std::string> asset_models; ///< [assets] models; plan_models() moves them into the model block
};

/// Plan every archive entry the manifest asks for, and refuse a plan whose flat
/// names collide.
///
/// Nothing is read, baked or compiled here — only names and paths are decided.
/// [assets] models are not in the item list: they wait in asset_models, and
/// plan_models() places them in the model block.
///
/// @param manifest     the validated manifest; assets and scripts are read
/// @param disc_dir     absolute, canonical disc directory the globs resolve against
/// @param texture_dir  directory baked .mppctex files will be written to
/// @param scripts_dir  directory compiled .luac files will be written to
/// @param sound_dir    directory baked .pcm files will be written to; .mp3 music tracks of [assets] go here too
/// @param out          receives the plan; untouched on failure
/// @param error        set with a message that already names its manifest section
/// @return RV_OK on success, the RV_ERR_* of the refusing step otherwise; .mp3 music tracks of [assets]
///         are planned as "<stem>.pcm" in sound_dir, baked by bake_music and copied as plain assets
int plan_archive(const rv_pdklib::rv_manifest &manifest,
    const std::filesystem::path &disc_dir,
    const std::filesystem::path &texture_dir,
    const std::filesystem::path &scripts_dir,
    const std::filesystem::path &sound_dir,
    archive_plan &out,
    std::string &error);

/// Append the [assets] models to a plan made by plan_archive, and refuse a plan whose names collide.
///
/// Kept apart from plan_archive so its callers that never bake models stay as they were.
///
/// @param disc_dir   absolute, canonical disc directory the globs resolve against
/// @param model_dir  directory baked .mppcmod files will be written to
/// @param plan       the plan to extend; untouched on failure
/// @param error      set with a message that already names its manifest section
/// @return RV_OK on success, the RV_ERR_* of the refusing step otherwise
int plan_models(const std::filesystem::path &disc_dir,
    const std::filesystem::path &model_dir,
    archive_plan &plan,
    std::string &error);

} // namespace rv_pdktools
