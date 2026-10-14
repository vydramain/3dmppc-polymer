#pragma once

#include <filesystem>
#include <string>

#include "rv_burner_assets/rv_burner_plan.hpp"
#include "pdklib/rv_manifest/rv_manifest.hpp"

namespace rv_pdktools
{

// --- baking, and the per-asset gate ---
//
// Every planned texture is handed to mppctexbaker, and every .mppctex that comes
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
/// Each entry in the plan's texture range is passed to mppctexbaker, whose output
/// lands at that entry's `payload`. One limit is enforced, and it is per
/// texture: the baked dimensions against `texture_max_width`/`texture_max_height`.
/// Nothing is accumulated across textures.
///
/// @param baker_hint  value of `--baker` (a directory, or a file in one); empty searches for mppctexbaker
/// @param manifest    the validated manifest; `textures_files.format` and `budget` are read
/// @param disc_dir    absolute disc directory the sources are relative to
/// @param plan        the planned archive; only its texture range is touched
/// @param error       set with the refusal, naming the file and the numbers
/// @return RV_OK on success; else the RV_ERR_* of the refusing step, `error` set
int bake_textures(const std::string &baker_hint,
    const rv_pdklib::rv_manifest &manifest,
    const std::filesystem::path &disc_dir,
    const archive_plan &plan,
    std::string &error);

/// Bake one PNG to .mppctex in the manifest's format, read it back and refuse it over the [budget].
///
/// The step bake_textures and bake_model_textures share: one mppctexbaker run, the baked file
/// read back whole, and its dimensions checked against texture_max_width/height.
///
/// @param baker     the mppctexbaker found by find_baker()
/// @param source    the PNG to bake
/// @param payload   where the .mppctex is written
/// @param manifest  `textures_files.format` is the --format; `budget` is the limit
/// @param label     how the entry is named in messages, e.g. its disc path
/// @param error     set with the refusal, naming the entry and the numbers
/// @return RV_OK on success; else the RV_ERR_* of the refusing step, `error` set
int bake_one_texture(const std::string &baker,
    const std::filesystem::path &source,
    const std::filesystem::path &payload,
    const rv_pdklib::rv_manifest &manifest,
    const std::string &label,
    std::string &error);

/// Bake every planned sound and refuse one whose baked .pcm is malformed.
///
/// Each entry in the plan's sound range is passed to mppcmusbaker, whose output
/// lands at that entry's `payload`. A baked .pcm is checked only for a whole
/// number of S16 samples; no budget applies to sounds.
///
/// @param baker_hint  value of `--baker` (a directory, or a file in one); empty searches for mppcmusbaker
/// @param disc_dir    absolute disc directory the sources are relative to
/// @param plan        the planned archive; only its sound range is touched
/// @param error       set with the refusal, naming the file
/// @return RV_OK on success; else the RV_ERR_* of the refusing step, `error` set
int bake_sounds(const std::string &baker_hint,
    const std::filesystem::path &disc_dir,
    const archive_plan &plan,
    std::string &error);

} // namespace rv_pdktools
