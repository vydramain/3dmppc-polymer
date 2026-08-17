// Need to explain file purpose
//
// The syntax is a SUBSET of TOML — sections, `key = "string"`, `key = 42`, and
// arrays of strings. Not a full TOML implementation, and the parser must say so
// when it meets something it does not handle rather than guessing: a manifest
// that is silently half-understood produces a disc that is silently wrong.
#pragma once

#include <cstdint>
#include <expected>
#include <string>
#include <vector>

namespace rv_pdktools
{

// One PNG-to-texel conversion rule. `files` are globs relative to the disc
// directory; `format` is the rv_texfmt to bake into, spelled "idx4", "idx8" or
// "direct15".
struct rv_manifest_textures {
	std::vector<std::string> files;
	std::string format = "idx8";
};

// The budget the burner enforces at pack time. Defaults are the REFERENCE
// console (docs/platform/specs.md); a disc built for a different machine states
// its own. Checking here is the whole point: a texture that does not fit is a
// error on the developer's desk, not a RV_ERR_INVAL on the player's loading
// screen.
struct rv_manifest_budget {
	int64_t texture_max_width = 256;
	int64_t texture_max_height = 256;
	int64_t video_memory_size = 1024 * 1024;
};

struct rv_manifest {
	// [disc]
	std::string disc_id;    // short machine name, e.g. "hello"
	std::string disc_title; // human title for the window and logs

	// [build] — globs relative to the disc directory
	std::vector<std::string> build_sources;
	std::vector<std::string> build_defines;
	std::vector<std::string> build_include_dirs;

	// [scripts] - globs relative lua scripts to the disc directory
	std::vector<std::string> scripts_sources;

	// [assets] — globs copied into the archive verbatim
	std::vector<std::string> assets_files;

	// [textures] — globs baked through mppcbaker on the way in
	rv_manifest_textures textures_files;

	// [budget]
	rv_manifest_budget budget;
};

// Parse manifest text: lexer → parser → semantic analysis → binder. Either a
// manifest or the diagnostics — never a half-filled manifest next to a flag the
// caller may forget to check. Every message NAMES THE LINE NUMBER, and one call
// reports every mistake it can, one per line: a manifest is written by hand, and
// "line 14: unknown key 'source' (did you mean 'sources'?)" is the difference
// between a fixed typo and an afternoon.
std::expected<rv_manifest, std::string> rv_manifest_parse(const std::string &text);

// Same, with the file name to stamp on each diagnostic: `disc.toml:14: ...`.
std::expected<rv_manifest, std::string> rv_manifest_parse(const std::string &text,
	const std::string &origin);

// Read `path` and parse it. Same contract, plus an I/O error message.
std::expected<rv_manifest, std::string> rv_manifest_load(const std::string &path);

// Render a manifest back to text, for the `inspect` subcommand and for writing
// the copy that goes into the archive.
std::string rv_manifest_render(const rv_manifest &manifest);

// Check the manifest describes a disc that can be burned at all: non-empty id,
// an id that is a safe filename, a known texture format, a positive
// abi_version. Returns true when sound, otherwise fills `error`.
bool rv_manifest_validate(const rv_manifest &manifest, std::string &error);

} // namespace rv_pdktools
