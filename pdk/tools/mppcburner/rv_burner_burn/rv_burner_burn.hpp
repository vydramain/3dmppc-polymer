#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

#include "rv_burner_assets/rv_burner_plan.hpp"
#include "pdklib/rv_manifest/rv_manifest.hpp"

namespace rv_pdktools
{

// --- writing the image ---
//
// The last phase, and the only one that produces the file the console loads.
// Everything it writes already exists on disk by now: the manifest was parsed,
// the module was compiled, the textures were baked and the scripts were
// compiled. This phase only decides what goes in and in which order.

/// Write the .mppcdisc image.
///
/// Three kinds of entry, in this order: the re-rendered manifest, the compiled
/// module, then every planned asset. The manifest is re-rendered from the parsed
/// structure rather than copied byte for byte, so the archive carries exactly
/// what the burner understood — a comment or a stray key the parser ignored
/// cannot ride along and mislead a later reader.
///
/// @param output_path  the .mppcdisc to create; its directory is created too
/// @param manifest     the validated manifest, re-rendered into the archive
/// @param disc_module  the disc.so compile_sources() produced
/// @param plan         the planned archive, written in its own order
/// @param burned_size  receives the size of the finished image, in bytes
/// @param error        set on any I/O failure; a partial image is removed
/// @return 0 on success, 1 on refusal
int burn_archive(
    const std::filesystem::path &output_path,
    const rv_pdklib::rv_manifest &manifest,
    const std::filesystem::path &disc_module,
    const archive_plan &plan,
    int64_t &burned_size,
    std::string &error);

/// Write the disc as an unpacked directory instead of a .mppcdisc.
///
/// Same three kinds of entry as burn_archive(), the same flat names, and the
/// same order does not matter here since a directory has no central index. The
/// difference is HOW each kind lands on disk: "disc.toml" is rendered text,
/// "disc.so" and a baked texture are finished products and are copied, but a
/// script or a verbatim-copied asset is symlinked straight back to its source
/// (falling back to a copy if the filesystem refuses the symlink) so that an
/// edit to the developer's own file is visible through the directory without
/// a rebuild. Every write lands at a temporary name first and is rename()'d
/// into place, so a failure mid-run cannot leave a half-written file at its
/// final name - but there is no promise over the directory AS A WHOLE: a crash
/// between two entries leaves some of them freshly published and others from
/// the previous run, which is fine for a namespace where every entry stands on
/// its own.
///
/// @param output_dir   the directory to publish into; created if missing
/// @param manifest     the validated manifest, re-rendered into "disc.toml"
/// @param disc_module  the disc.so compile_sources() produced
/// @param plan         the planned archive; for an unpacked build the caller
///                     has already pointed script entries at their .lua
///                     source instead of compiled bytecode
/// @param error        set on any I/O failure
/// @return 0 on success, 1 on refusal
int burn_directory(
    const std::filesystem::path &output_dir,
    const rv_pdklib::rv_manifest &manifest,
    const std::filesystem::path &disc_module,
    const archive_plan &plan,
    std::string &error);

} // namespace rv_pdktools
