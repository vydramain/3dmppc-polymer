#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <utility> // IWYU pragma: TODO: write reason
#include <vector>

#include "rv_burner_options.hpp"
#include "rv_manifest.hpp"

namespace rv_pdktools
{

// Burn a disc directory into one .mppcdisc. Returns a process exit code: 0 on
// success, 1 on any refusal. Progress and diagnostics go to stderr, nothing of
// this command goes to stdout.
int rv_build_run(const rv_burner_options &options);

// Print what is inside an already-burned .mppcdisc, without unpacking it and
// without writing anything anywhere. The listing goes to stdout, errors to
// stderr. Same exit-code contract.
int rv_inspect_run(const rv_burner_options &options);

// ── Pieces exposed so a harness can exercise them without a disc directory ────

// Expand `patterns` (globs relative to `root`, '/'-separated, `*`, `?` and `**`
// understood) into a sorted, duplicate-free list of relative file paths. A
// pattern that matches nothing is an ERROR, not an empty result: in a manifest
// written by hand it is a typo every time, and silently building a disc without
// the file the author listed is the worst possible reading of it.
bool rv_glob_expand(const std::filesystem::path &root,
	const std::vector<std::string> &patterns,
	std::vector<std::string> &out,
	std::string &error);

// The name a relative path takes inside the archive. See the THEOREM in
// rv_build.cpp: the asset namespace is FLAT, so this is just the file name.
std::string rv_flat_name(const std::string &relative_path);

// Is `name` allowed as an archive entry at all? Rejects path separators, a
// leading dot, empty names, and the service names the loader reserves.
bool rv_check_asset_name(const std::string &name, std::string &error);

// One planned archive entry. `source` is the file in the disc directory the
// developer wrote — kept because a collision diagnostic is useless unless it
// names BOTH originals, and "two .mppctex files collide" says nothing about
// which two PNGs to rename. `payload` is where the bytes actually come from:
// the same file for a copied asset, the baked .mppctex for a texture.
struct rv_archive_item {
	std::string name;
	std::string source;
	std::string payload;
};

// Refuse when two different originals collapse onto one flat name. See the
// THEOREM in rv_build.cpp.
bool rv_check_collisions(const std::vector<rv_archive_item> &items, std::string &error);

// Render the CMakeLists.txt of the generated disc project. Pure text in, pure
// text out, so the harness can read what the tool would have written.
std::string rv_cmake_project_text(const rv_manifest &manifest,
	const std::vector<std::string> &absolute_sources,
	const std::string &pdk_dir, const std::string &pdklib_dir,
	const std::vector<std::string> &absolute_includes);

// Human size for a progress line ("1.2 MB"). Exposed only because it is easier
// to test than to eyeball.
std::string rv_human_size(int64_t bytes);

} // namespace rv_pdktools
