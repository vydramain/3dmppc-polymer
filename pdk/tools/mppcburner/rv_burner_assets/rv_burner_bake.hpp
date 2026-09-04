#pragma once

#include <filesystem>
#include <string>

#include "rv_burner_assets/rv_burner_plan.hpp"
#include "pdklib/rv_manifest/rv_manifest.hpp"

namespace rv_pdktools
{

// --- baking, and the per-asset gate ---
//
// Every planned texture is handed to mppcbaker, and every .mppctex that comes
// back is read again before it is trusted. The re-read is the point: the shape
// that is checked has to be the shape that will actually be uploaded, and only
// the baked file states it — this burner decodes no PNG, so without the re-read
// it would be checking a number it guessed.
//
// A PER-ASSET limit is checked HERE and not by the console because a console can
// only answer with RV_ERR_INVAL on a loading screen, in front of a player. The
// burner can answer with a number, on the developer's terminal, before the disc
// exists.
//
// What is deliberately NOT checked here is how much video memory the disc holds
// AT ONCE. A disc reserves and releases regions while it runs, so the sum of
// every texture the archive carries is an upper bound that never exists at
// runtime, and refusing on it would refuse discs that run. That question belongs
// where it actually happens: rv_cv answers RV_ERR_NOMEM when a reservation does
// not fit.

/// Bake every planned texture and refuse one that overruns its [budget].
///
/// Each entry in the plan's texture range is passed to mppcbaker, whose output
/// lands at that entry's `payload`. One limit is enforced, and it is per
/// texture: the baked dimensions against `texture_max_width`/`texture_max_height`.
/// Nothing is accumulated across textures.
///
/// @param baker_hint  value of `--baker`; empty means find mppcbaker
/// @param manifest    the validated manifest; `textures.format` and `budget` are read
/// @param disc_dir    absolute disc directory the sources are relative to
/// @param plan        the planned archive; only its texture range is touched
/// @param error       set with the refusal, naming the file and the numbers
/// @return 0 on success, 1 on refusal
int bake_textures(
    const std::string &baker_hint,
    const rv_pdklib::rv_manifest &manifest,
    const std::filesystem::path &disc_dir,
    const archive_plan &plan,
    std::string &error);

} // namespace rv_pdktools
