#pragma once

#include <string>
#include <vector>

namespace rv_pdktools
{

// --- flat names ---
//
// The console resolves a resource by name and rv_cd forbids path separators in
// that name, so the archive namespace is FLAT: `sprites/tex.png` enters it as
// `tex.png`, and so does `models/tex.png`. Two directories can therefore name
// one archive entry.
//
// The policy is fail-fast, not last-write-wins. Overwriting would produce a disc
// that loads the wrong artist's file with no trace of why, in either the source
// tree or the running game; and the console cannot diagnose it, because by the
// time the archive exists the other file is simply gone. So the burner refuses
// and names both originals — the fix is one rename.

/// One planned entry of the archive, filled in before any work is done.
///
/// `source` exists only for diagnostics: a collision message is useless unless
/// it names BOTH originals, and "two .mppctex files collide" says nothing about
/// which two PNGs to rename.
struct archive_item {
    std::string name;    ///< flat name the console will ask for
    std::string source;  ///< the author's file, relative to the disc directory
    std::string payload; ///< where the bytes come from: the original for a
                         ///< copied asset, the baked or compiled file otherwise
};

/// The flat name a relative path takes inside the archive.
///
/// @param relative_path  a path relative to the disc directory
/// @return its file name, with every directory stripped
std::string flat_name(const std::string &relative_path);

/// Is @p name allowed as an archive entry at all?
///
/// Refuses an empty name, a path separator, a leading dot (dotfiles are editor
/// and VCS bookkeeping, not disc content) and the two service names the loader
/// reads before it looks at the asset namespace.
///
/// @param name   a flat name, normally from flat_name()
/// @param error  set with the reason when the name is refused
/// @return true when the name may enter the archive
bool check_asset_name(const std::string &name, std::string &error);

/// Refuse a plan in which two different originals collapse onto one flat name.
///
/// @param items  the whole planned archive, in any order
/// @param error  set naming the flat name and both originals
/// @return true when every flat name in @p items is unique
bool check_collisions(const std::vector<archive_item> &items, std::string &error);

} // namespace rv_pdktools
