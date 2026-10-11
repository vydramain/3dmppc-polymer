#pragma once

#include <filesystem>
#include <string>

#include "rv_burner_assets/rv_burner_plan.hpp"

namespace rv_pdktools
{

/// Bake every planned model to .mppcmod and refuse one the console would not parse.
///
/// Each entry in the plan's model range is passed to mppcmodbaker, whose output lands
/// at that entry's `payload`. The baked file is read back whole and checked with
/// rv_mppcmod_parse, the same parser the console uses. Textures the baker wrote
/// beside the .mppcmod are left where they are.
///
/// @param baker_hint  value of `--baker` (a directory, or a file in one); empty searches for mppcmodbaker
/// @param disc_dir    absolute disc directory the sources are relative to
/// @param plan        the planned archive; only its model range is touched
/// @param error       set with the refusal, naming the baker or the baked file
/// @return RV_OK on success; else the RV_ERR_* of the refusing step, `error` set
int bake_models(const std::string &baker_hint,
    const std::filesystem::path &disc_dir,
    const archive_plan &plan,
    std::string &error);

} // namespace rv_pdktools
