#pragma once

#include "pdk/cv/rv_texture.hpp"
#include <cstdint>
#include <string>
#include <vector>

namespace rv_pdklib
{

// One PNG-to-texel conversion rule. `files` are globs relative to the disc
// directory; `format` is the rv_texfmt to bake into. The manifest spells it as
// text and the binder turns that text into the enumerator — the accepted
// spellings are the rows of rv_texfmt_names (pdklib/rv_textures/rv_texfmt_name.hpp) and are not
// restated anywhere in this tool.
struct rv_manifest_textures {
    std::vector<std::string> files;
    rv_pdk::rv_texfmt format;
};

// The budget the burner enforces at pack time. There are no defaults on
// purpose: the numbers describe the machine the disc is built for, and a value
// the tool invented is a guess printed on the disc as if it were a decision.
// A manifest without a [budget] section arrives here all-zero and is refused.
// Checking here is the whole point: a texture that does not fit is an error on
// the developer's desk, not a RV_ERR_INVAL on the player's loading screen.
struct rv_manifest_budget {
    int64_t texture_max_width;
    int64_t texture_max_height;
    int64_t video_memory_size;
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
int rv_manifest_parse(const std::string &text,
    rv_manifest &manifest,
    std::string &error);

// Same, with the file name to stamp on each diagnostic: `disc.toml:14: ...`.
int rv_manifest_parse(const std::string &text,
    const std::string &origin,
    rv_manifest &manifest,
    std::string &error);

// Read `path` and parse it. Same contract, plus an I/O error message.
int rv_manifest_load(const std::string &path,
    rv_manifest &manifest,
    std::string &error);

// Render a manifest back to text, for the `inspect` subcommand and for writing
// the copy that goes into the archive.
std::string rv_manifest_render(const rv_manifest &manifest);

// Check the manifest describes a disc that can be burned at all: non-empty id,
// an id that is a safe filename, a known texture format, and a [budget] that
// is present with every field positive. Returns true when sound, otherwise
// fills `error`.
bool rv_manifest_validate(const rv_manifest &manifest, std::string &error);

} // namespace rv_pdklib
