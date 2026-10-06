#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "rv_burner_assets/rv_burner_plan.hpp"
#include "pdklib/rv_manifest/rv_manifest.hpp"

namespace rv_pdktools
{

/// Which entry of the disc each of the author's files became,
/// so a tool never restates the naming rules. The first line is
/// `mppcburner-map <PDK version>`, e.g.
/// `mppcburner-map 1.0`; then one line per
/// entry, its fields separated by TABs so that a path may hold spaces:
///
///     <source, relative to the disc directory> <kind> <entry name> [<parameter>]
///
/// kind is `code` (a [build] source; the entry is disc.so), `file` (copied as
/// it is), `texture` (the parameter is the format it was baked to), `sound`
/// (the parameter is the PCM shape the baker writes, `s16le-44100-mono`),
/// `entry` (the lua script_entry) or `module` (another script; the parameter
/// is the name `require` takes). Written only after a build that succeeded,
/// through a temporary file and a rename.
///
/// @return 0 on success, 1 with the reason in @p error
int write_map(const std::filesystem::path &path,
    const rv_pdklib::rv_manifest &manifest,
    const std::vector<std::string> &sources,
    const archive_plan &plan,
    std::string &error);

} // namespace rv_pdktools
