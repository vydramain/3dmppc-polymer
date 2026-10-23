#pragma once

#include <filesystem>
#include <string>

#include "pdklib/rv_manifest/rv_manifest.hpp"
#include "rv_burner_options.hpp"

namespace rv_pdktools
{

// --- shared by both subcommands ---
//
// [1/4] manifest is the first step of `build` and the whole of what
// `bake-texture` needs before it can pick a texture, so both call this one
// definition (rv_burner_build_runner.cpp) instead of loading disc.toml twice.
int rv_burner_build_manifest(const std::filesystem::path &disc_dir, rv_pdklib::rv_manifest &manifest, std::string &error);

// --- the burn pipeline ---
//
// One disc directory in, one .mppcdisc out, in four phases:
//
//   [1/4] manifest  parse and validate disc.toml
//   [2/4] compile   generate a CMake project and build it into disc.so
//   [3/4] assets    plan the archive, bake textures, compile scripts
//   [4/4] burn      write the image
//
// Each phase lives in its own directory and knows nothing about the others; what
// they share sits in rv_burner_common/. This function is the only place the
// order is written down, and the only place that prints the ladder - a phase
// reports by returning RV_OK or an RV_ERR_* kind and setting an error string, and does not print:
// the wrappers in rv_burner_build_runner.cpp print the reason before they return the same code.

/// Burn a disc directory into the target options.target names.
///
/// Reads `options.operand` as the disc directory. What it writes is the target
/// `options.target` names (rv_burner_target.hpp): disc writes the image at
/// `options.output`, or an unpacked directory at `options.unpacked`; player
/// writes a directory at `options.output` holding the image and the console.
/// Progress and diagnostics go to stderr; nothing goes to stdout.
///
/// The generated build tree is removed afterwards unless `--keep-build` was
/// given, and is always kept after a failure, where it is the thing a developer
/// needs to look at.
///
/// @param options  the parsed command line
/// @return RV_OK on success; otherwise the RV_ERR_* kind of the refusal, which main passes on as the exit status.
///         The shell sees a negative code modulo 256 (RV_ERR_INVAL, -1, reads as 255).
int rv_burner_build_run(const rv_burner_options &options);

// --- one texture ---
//
// Bakes exactly one texture the way [3/4] assets of `build` would: same
// manifest load and validation, same [assets] texture selection, same mppctexbaker
// invocation and budget check.

/// Bake one texture of a disc into a standalone .mppctex.
///
/// Reads `options.operand` as the disc directory, `options.bake_source` as
/// the source file (relative to the disc directory, or absolute inside it)
/// and `options.output` as the file to write. The source must be one of the
/// PNG files `[assets]` lists; refused otherwise.
///
/// On success the texture's flat archive name is the only thing written to
/// stdout. Progress and diagnostics go to stderr.
///
/// @param options  the parsed command line
/// @return RV_OK on success; otherwise the RV_ERR_* kind of the refusal, which main passes on as the exit status
int rv_burner_bake_texture_run(const rv_burner_options &options);

} // namespace rv_pdktools
