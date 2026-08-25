#pragma once

#include <cstddef>
#include <filesystem>
#include <string>

#include "rv_burner_common/rv_burner_archive.hpp"
#include "rv_burner_manifest/rv_burner_manifest.hpp"

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
/// later stages find their own entries again: textures go to mppcbaker, scripts
/// go to luajit, copied assets need neither.
struct archive_plan {
    std::vector<archive_item> items; ///< every entry, in archive order

    std::size_t asset_count = 0;    ///< copied verbatim; they lead the vector
    std::size_t first_texture = 0;  ///< index of the first texture entry
    std::size_t texture_count = 0;  ///< how many texture entries follow it
    std::size_t first_script = 0;   ///< index of the first script entry
    std::size_t script_count = 0;   ///< how many script entries follow it
};

/// Plan every archive entry the manifest asks for, and refuse a plan whose flat
/// names collide.
///
/// Nothing is read, baked or compiled here — only names and paths are decided.
///
/// @param manifest     the validated manifest; assets, textures and scripts are read
/// @param disc_dir     absolute, canonical disc directory the globs resolve against
/// @param texture_dir  directory baked .mppctex files will be written to
/// @param scripts_dir  directory compiled .luac files will be written to
/// @param out          receives the plan; untouched on failure
/// @param error        set with a message that already names its manifest section
/// @return 0 on success, 1 on refusal
int plan_archive(
    const rv_burner_manifest &manifest,
    const std::filesystem::path &disc_dir,
    const std::filesystem::path &texture_dir,
    const std::filesystem::path &scripts_dir,
    archive_plan &out,
    std::string &error);

} // namespace rv_pdktools
