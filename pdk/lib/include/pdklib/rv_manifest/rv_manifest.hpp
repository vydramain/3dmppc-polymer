#pragma once

#include "pdk/cv/rv_texture.h"
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
    rv_texfmt format;
};

// --- the defaults -------------------------------------------------------------
//
// Every number below IS the reference machine — there is no other copy of
// these numbers, this struct's defaults are the specification. A manifest
// states only what it wants to differ from;
// everything it leaves out is filled in from here, and what it gets is exactly
// the machine the built-in disc runs on. There is one copy of these numbers and
// this is it — the console's built-in budget is a default-constructed
// rv_manifest_budget rather than a second list that can drift from this one.
//
// The defaulted operator== on each section is what rendering uses to decide
// whether a section is worth writing at all: a section equal to its default was
// not a decision, and printing it would put the tool's own answer on the disc
// dressed up as the author's.

struct rv_manifest_budget_pcca {
    int64_t voice_count = 24;
    int64_t sound_memory_size = 512 * 1024;

    bool operator==(const rv_manifest_budget_pcca &) const = default;
};

struct rv_manifest_budget_pccv {
    int64_t screen_width = 320;
    int64_t screen_height = 240;
    int64_t texture_max_width = 256;
    int64_t texture_max_height = 256;
    int64_t video_memory_size = 1024 * 1024;
    int64_t frame_capacity = 4096;
    int64_t ot_bucket_count = 1024;

    bool operator==(const rv_manifest_budget_pccv &) const = default;
};

struct rv_manifest_budget_pccio {
    int64_t iport_count = 2;

    bool operator==(const rv_manifest_budget_pccio &) const = default;
};

struct rv_manifest_budget_pccm {
    int64_t card_slots = 16;
    int64_t card_slot_size = 8 * 1024;

    bool operator==(const rv_manifest_budget_pccm &) const = default;
};

struct rv_manifest_budget_pccd {
    // Empty means the conventional entry name, decided by the loader.
    std::string code_entry;

    bool operator==(const rv_manifest_budget_pccd &) const = default;
};

// The lua machine. Its default is NO MACHINE — the reference disc carries no
// scripts, so the reference answer here is zero, exactly as 24 voices is the
// reference answer for pcca. Scripting is not an exception to the default rule;
// it is the rule applied to a subsystem whose reference value happens to be
// "absent".
struct rv_manifest_budget_pccl {
    int64_t script_memory_size = 0;

    // The one script the disc starts its scripting from. Named rather than
    // guessed, for the same reason pccd names its module: the drive is asked
    // for it verbatim. Spelled script_entry, not code_entry, so that the two
    // never read as the same thing at a glance — they are not.
    //
    // This does NOT replace pccd's code_entry. Every disc has a disc.so — it
    // carries the ELF note the version and checksum are read from, it is what
    // pccd starts, and its RAII is what ends the run. Scripting is reached
    // FROM INSIDE it: disc.so initializes, then hands this entry to the lua
    // machine. Whatever that script pulls in afterwards is between it and the
    // drive; the console neither knows nor counts it.
    //
    // This name and script_memory_size are ONE declaration in two fields —
    // together with [scripts] sources it is one declaration in three: all
    // three present or all three absent. Checked at burn and again at mount.
    // Half of it is not a smaller lua machine, it is a broken disc.
    std::string script_entry;

    bool operator==(const rv_manifest_budget_pccl &) const = default;
};

// The budget the burner enforces at pack time. A manifest that states none of
// it describes the reference machine, and that is a decision the author can
// legitimately make by saying nothing — the numbers are the console's own, not
// a guess. What a manifest DOES state overrides its part and nothing else.
//
// Checking here is still the whole point: a texture that does not fit is an
// error on the developer's desk, not a RV_ERR_INVAL on the player's loading
// screen.
struct rv_manifest_budget {
    rv_manifest_budget_pcca pcca;
    rv_manifest_budget_pccv pccv;
    rv_manifest_budget_pccio pccio;
    rv_manifest_budget_pccm pccm;
    rv_manifest_budget_pccd pccd;
    rv_manifest_budget_pccl pccl;
};

struct rv_manifest {
    // [disc]
    std::string disc_id;    // short machine name, e.g. "example-cpp"
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
