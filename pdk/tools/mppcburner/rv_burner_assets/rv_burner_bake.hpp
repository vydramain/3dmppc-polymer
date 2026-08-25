#pragma once

#include <filesystem>
#include <string>

#include "rv_burner_assets/rv_burner_plan.hpp"
#include "rv_burner_manifest/rv_burner_manifest.hpp"

namespace rv_pdktools
{

// --- baking, and the budget gate ---
//
// Every planned texture is handed to mppcbaker, and every .mppctex that comes
// back is read again before it is trusted. The re-read is the point: the budget
// has to be checked against the TEXELS that will actually be uploaded, which is
// something only the baked file knows — the PNG says nothing about the format
// the manifest chose, and a guess made from it would be wrong for IDX4.
//
// A budget is checked HERE and not by the console because a console can only
// answer with RV_ERR_INVAL on a loading screen, in front of a player. The burner
// can answer with a number, on the developer's terminal, before the disc exists.

/// Bake every planned texture and refuse a disc that overruns its [budget].
///
/// Each entry in the plan's texture range is passed to mppcbaker, whose output
/// lands at that entry's `payload`. Two limits are enforced: each texture
/// against `texture_max_width`/`texture_max_height`, and the sum of all texels
/// and palettes against `video_memory_size`. The sum is an upper bound — a game
/// that frees one texture before loading the next uses less — so it is the
/// conservative reading, and a disc that trips it is told by how much.
///
/// @param baker_hint  value of `--baker`; empty means find mppcbaker
/// @param manifest    the validated manifest; `textures.format` and `budget` are read
/// @param disc_dir    absolute disc directory the sources are relative to
/// @param plan        the planned archive; only its texture range is touched
/// @param error       set with the refusal, naming the file and the numbers
/// @return 0 on success, 1 on refusal
int bake_textures(
    const std::string &baker_hint,
    const rv_burner_manifest &manifest,
    const std::filesystem::path &disc_dir,
    const archive_plan &plan,
    std::string &error);

} // namespace rv_pdktools
