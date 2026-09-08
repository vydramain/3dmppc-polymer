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

} // namespace rv_pdktools
