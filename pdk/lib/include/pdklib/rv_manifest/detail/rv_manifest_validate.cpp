// The checks a parsed manifest must pass before a disc can be burned; rv_manifest_validate is the entry point.
#include "../rv_manifest.hpp"

#include <cctype>
#include <cstdint>
#include <format>
#include <set>
#include <string>

#include "pdk/rv_err.h"
#include "pdklib/rv_textures/rv_texfmt_name.hpp"

#include "rv_manifest_binder.hpp"
#include "rv_manifest_sdk.hpp"

namespace rv_pdklib
{
// One name the drive will be asked for verbatim. Empty passes: it means the
// key was not written, and each caller decides what that absence means.
static int safe_entry_name(const std::string &entry, const char *what, std::string &error)
{
    if (entry.empty()) {
        return RV_OK;
    }
    if (entry.front() == '/' || entry.find("..") != std::string::npos || entry.find('/') != std::string::npos ||
        entry.find('\\') != std::string::npos) {
        error = std::string(what) + " '" + entry + "' is not a safe entry name - it must not be a path";
        return RV_ERR_INVAL;
    }
    for (char c : entry) {
        const unsigned char u = static_cast<unsigned char>(c);
        if (std::isalnum(u) == 0 && c != '-' && c != '_' && c != '.') {
            error =
                std::string(what) + " '" + entry + "' is not a safe entry name - use letters, digits, '-', '_' and '.' only";
            return RV_ERR_INVAL;
        }
    }
    return RV_OK;
}

// The id is not decoration: it becomes the name of the burned file and of
// the directory the console unpacks into, so it is checked as a FILENAME
// before anything else touches it. A '/' would escape the output directory,
// ".." would climb out of it, and a leading '.' would hide the result.
static int validate_disc_id(const rv_manifest &manifest, std::string &error)
{
    if (manifest.disc_id.empty()) {
        error = "[disc] id is empty - the disc needs a short machine name";
        return RV_ERR_INVAL;
    }
    for (char c : manifest.disc_id) {
        const unsigned char u = static_cast<unsigned char>(c);
        if (std::isalnum(u) == 0 && c != '-' && c != '_') {
            error = "[disc] id '" + manifest.disc_id + "' is not a safe filename - use letters, digits, '-' and '_' only";
            return RV_ERR_INVAL;
        }
    }
    if (manifest.disc_id.front() == '.') { // unreachable through the loop above, kept as a guard
        error = "[disc] id must not start with '.'";
        return RV_ERR_INVAL;
    }
    return RV_OK;
}

// The rest of what a disc cannot be built without. Sections with a reference
// value may be left unsaid; these two have none, because no answer the tool
// could invent would be the author's. [assets] is not here: a disc may carry
// no assets at all.
static int validate_required_sections(const rv_manifest &manifest, std::string &error)
{
    if (manifest.disc_title.empty()) {
        error = "[disc] title is empty - the disc needs a human title for the window and logs";
        return RV_ERR_INVAL;
    }
    if (manifest.build_sources.empty()) {
        error = "[build] sources is empty - a disc is built from its own code";
        return RV_ERR_INVAL;
    }
    return RV_OK;
}

// pccd.code_entry and pccl.script_entry both name something the drive is
// later asked for BY NAME
// - an empty value is legal and means "use the conventional name", but
// anything that IS given must be a bare name: no path to climb out of the
// medium with.
static int validate_entry_names(const rv_manifest &manifest, std::string &error)
{
    int code;

    code = safe_entry_name(manifest.budget.pccd.code_entry, "[budget.pccd] code_entry", error);
    if (code != RV_OK) {
        return code;
    }

    code = safe_entry_name(manifest.budget.pccl.script_entry, "[budget.pccl] script_entry", error);
    if (code != RV_OK) {
        return code;
    }

    return RV_OK;
}

// Guarantees on [build] sdk after validation: each name is a plain word (see
// rv_manifest_sdk.hpp), and each tree is named once.
static int validate_sdk_names(const rv_manifest &manifest, std::string &error)
{
    std::set<std::string> seen;
    for (const std::string &name : manifest.build_sdk) {
        if (!rv_manifest_sdk_name_is_valid(name)) {
            error =
                "[build] sdk '" + name + "' is not a tree name - use a lowercase letter, then lowercase letters, digits or '_'";
            return RV_ERR_INVAL;
        }
        if (!seen.insert(name).second) {
            error = "[build] sdk '" + name + "' is listed twice - name each tree once";
            return RV_ERR_INVAL;
        }
    }
    return RV_OK;
}

// The accepted format spellings, read from the table, so a message never lists them by hand.
static std::string texfmt_spellings()
{
    std::string known;
    for (const rv_pdklib::rv_texfmt_name &row : rv_pdklib::rv_texfmt_names) {
        if (!known.empty()) {
            known += ", ";
        }
        known += row.text;
    }
    return known;
}

// [assets.texture] format is optional and defaults to idx8, so only a spelling the binder could not
// read is refused: it arrives as the unknown-spelling marker, and nothing else can.
static int validate_assets_texture_format(const rv_manifest &manifest, std::string &error)
{
    if (manifest.assets_texture_format != rv_manifest_texfmt_unknown_spelling) {
        return RV_OK;
    }
    error = std::format("[assets.texture] format is not a known spelling - expected one of {}", texfmt_spellings());
    return RV_ERR_INVAL;
}

// Every resource the machine has to supply. A zero here is not a value the
// disc chose, it is a field nobody wrote. Every field here now arrives
// filled: absent means the reference machine, not zero. So a zero or a
// negative can only be something the author typed, and that is the only
// thing left to refuse.
static int validate_budget_positive(const rv_manifest &manifest, std::string &error)
{
    struct budget_field {
        const char *name;
        int64_t value;
    };
    const rv_manifest_budget &budget = manifest.budget;
    const budget_field budgets[] = {
        { "[budget.pcca] voice_count", budget.pcca.voice_count },
        { "[budget.pcca] sound_memory_size", budget.pcca.sound_memory_size },
        { "[budget.pccv] screen_width", budget.pccv.screen_width },
        { "[budget.pccv] screen_height", budget.pccv.screen_height },
        { "[budget.pccv] texture_max_width", budget.pccv.texture_max_width },
        { "[budget.pccv] texture_max_height", budget.pccv.texture_max_height },
        { "[budget.pccv] video_memory_size", budget.pccv.video_memory_size },
        { "[budget.pccv] frame_capacity", budget.pccv.frame_capacity },
        { "[budget.pccv] ot_bucket_count", budget.pccv.ot_bucket_count },
        { "[budget.pccg] model_memory_size", budget.pccg.model_memory_size },
        { "[budget.pccg] triangle_capacity", budget.pccg.triangle_capacity },
        { "[budget.pccio] iport_count", budget.pccio.iport_count },
        { "[budget.pccm] card_slots", budget.pccm.card_slots },
        { "[budget.pccm] card_slot_size", budget.pccm.card_slot_size },
    };
    for (const budget_field &b : budgets) {
        if (b.value <= 0) {
            error = std::format("{} must be positive, got {}", b.name, b.value);
            return RV_ERR_INVAL;
        }
    }
    return RV_OK;
}

// pccl is deliberately NOT in the budget table. Every other subsystem is
// hardware the machine always has; the Lua machine exists only for a disc
// that carries scripts, so an absent section is a legal statement rather
// than a missing one. Only a negative is nonsense - nobody asks for less
// than no memory.
static int validate_pccl_memory(const rv_manifest &manifest, std::string &error)
{
    if (manifest.budget.pccl.script_memory_size < 0) {
        error = std::format("[budget.pccl] script_memory_size must not be negative, got {}",
            manifest.budget.pccl.script_memory_size);
        return RV_ERR_INVAL;
    }
    return RV_OK;
}

// A lua disc is declared by THREE statements that mean nothing apart:
// where the scripts come from, how much memory they run in, and which one
// starts. All three, or none of them - a disc is a lua disc or a C++ disc,
// and there is no state between the two. Any partial declaration is a
// manifest that describes a machine nobody can build.
static int validate_lua_disc_consistency(const rv_manifest &manifest, std::string &error)
{
    const bool has_scripts = !manifest.scripts_sources.empty();
    const bool has_memory = manifest.budget.pccl.script_memory_size > 0;
    const bool has_entry = !manifest.budget.pccl.script_entry.empty();

    if (has_scripts == has_memory && has_memory == has_entry) {
        return RV_OK;
    }

    std::string stated;
    std::string missing;
    const auto note = [&](bool present, const char *what) {
        std::string &side = present ? stated : missing;
        if (!side.empty()) {
            side += ", ";
        }
        side += what;
    };
    note(has_scripts, "[scripts] sources");
    note(has_memory, "[budget.pccl] script_memory_size");
    note(has_entry, "[budget.pccl] script_entry");

    error = std::format("a lua disc states all three of [scripts] sources, [budget.pccl] script_memory_size "
                        "and [budget.pccl] script_entry; this manifest states {} and leaves out {}. "
                        "State the rest, or drop them all and burn a C++ disc",
        stated,
        missing);
    return RV_ERR_INVAL;
}

int rv_manifest_validate(const rv_manifest &manifest, std::string &error)
{
    error.clear();

    int code;

    code = validate_disc_id(manifest, error);
    if (code != RV_OK) {
        return code;
    }

    code = validate_required_sections(manifest, error);
    if (code != RV_OK) {
        return code;
    }

    code = validate_entry_names(manifest, error);
    if (code != RV_OK) {
        return code;
    }

    code = validate_sdk_names(manifest, error);
    if (code != RV_OK) {
        return code;
    }

    code = validate_assets_texture_format(manifest, error);
    if (code != RV_OK) {
        return code;
    }

    code = validate_budget_positive(manifest, error);
    if (code != RV_OK) {
        return code;
    }

    code = validate_pccl_memory(manifest, error);
    if (code != RV_OK) {
        return code;
    }

    code = validate_lua_disc_consistency(manifest, error);
    if (code != RV_OK) {
        return code;
    }

    return RV_OK;
}

} // namespace rv_pdklib
