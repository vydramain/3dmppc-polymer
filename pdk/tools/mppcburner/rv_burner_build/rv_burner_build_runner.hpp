#pragma once

#include "rv_burner_options.hpp"

namespace rv_pdktools
{

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
// order is written down, and the only place that prints the ladder — a phase
// reports by returning 0 or 1 and setting an error string, never by printing.

/// Burn a disc directory into one .mppcdisc image.
///
/// Reads `options.operand` as the disc directory and `options.output` as the
/// image to write. Progress and diagnostics go to stderr; nothing goes to
/// stdout, because the product of this command is a file, not a stream.
///
/// The generated build tree is removed afterwards unless `--keep-build` was
/// given, and is always kept after a failure, where it is the thing a developer
/// needs to look at.
///
/// @param options  the parsed command line
/// @return a process exit code: 0 on success, 1 on any refusal
int rv_burner_build_run(const rv_burner_options &options);

// --- one texture ---
//
// Bakes exactly one texture the way [3/4] assets of `build` would: same
// manifest load and validation, same [textures] selection, same mppcbaker
// invocation and budget check.

/// Bake one texture of a disc into a standalone .mppctex.
///
/// Reads `options.operand` as the disc directory, `options.bake_source` as
/// the source file (relative to the disc directory, or absolute inside it)
/// and `options.output` as the file to write. The source must be one of the
/// files `[textures]` selects; refused otherwise.
///
/// On success the texture's flat archive name is the only thing written to
/// stdout. Progress and diagnostics go to stderr.
///
/// @param options  the parsed command line
/// @return a process exit code: 0 on success, 1 on any refusal
int rv_burner_bake_texture_run(const rv_burner_options &options);

} // namespace rv_pdktools
