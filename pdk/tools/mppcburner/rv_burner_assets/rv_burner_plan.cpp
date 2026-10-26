#include "rv_burner_plan.hpp"

#include <algorithm>
#include <array>
#include <filesystem>
#include <vector>

#include "pdk/rv_err.h"
#include "pdklib/rv_manifest/rv_manifest_asset_kind.hpp"
#include "pdklib/rv_models/rv_mppcmod.hpp"
#include "rv_burner_common/rv_burner_globs.hpp"

namespace fs = std::filesystem;

namespace
{

// What a block's entries are written as: where the produced file goes and what it is called.
struct entry_rule {
    fs::path payload_dir;                              // empty: the original file IS the payload
    const char *extension;                             // replaces the suffix, e.g. ".luac"
    std::string (*name_of)(const std::string &source); // builds the disc name; wins over extension
};

// Every [assets] file is diagnosed under this name, whichever block it lands in.
constexpr const char *k_assets_section = "[assets] files";
constexpr const char *k_stream_section = "[assets.stream] files";

// One slot per rv_manifest_asset_kind; copy is the last kind, so the count is its index plus one.
constexpr std::size_t k_asset_kind_count = static_cast<std::size_t>(rv_pdklib::rv_manifest_asset_kind::copy) + 1;

std::size_t kind_slot(rv_pdklib::rv_manifest_asset_kind kind)
{
    return static_cast<std::size_t>(kind);
}

} // namespace

namespace rv_pdktools
{

// Expand one section's globs. An empty list expands to nothing and is not an error.
//
// @param patterns  the section's globs
// @param section   section name for diagnostics, e.g. "[scripts] sources"
// @param disc_dir  directory the globs resolve against
// @param matched   receives the relative paths, sorted
// @param error     set with the section name already prefixed
// @return RV_OK on success, the RV_ERR_* of the glob otherwise
static int glob_section(const std::vector<std::string> &patterns,
    const char *section,
    const fs::path &disc_dir,
    std::vector<std::string> &matched,
    std::string &error)
{
    matched.clear();
    if (patterns.empty()) {
        return RV_OK;
    }

    const int glob_err = glob_expand(disc_dir, patterns, matched, error);
    if (glob_err != RV_OK) {
        error = std::string(section) + ": " + error;
    }
    return glob_err;
}

// Append one entry per relative path, in the order given.
//
// @param relatives  paths relative to disc_dir
// @param section    section name for diagnostics
// @param disc_dir   directory the paths resolve against
// @param rule       how the entries are written
// @param plan       receives the entries, appended in order
// @param error      set with the section name already prefixed
// @return RV_OK on success, the RV_ERR_* of the refusing step otherwise
static int append_entries(const std::vector<std::string> &relatives,
    const char *section,
    const fs::path &disc_dir,
    const entry_rule &rule,
    archive_plan &plan,
    std::string &error)
{
    for (const std::string &relative : relatives) {
        archive_item item;

        // A produced file keeps the author's file name and changes only its
        // suffix, so `ui/menu.lua` and `menu.luac` are recognisably the same
        // thing in a diagnostic. A copied file keeps its name entirely.
        if (rule.name_of != nullptr) {
            item.name = rule.name_of(relative);
        } else if (rule.extension != nullptr) {
            item.name = fs::path(relative).stem().string() + rule.extension;
        } else {
            item.name = flat_name(relative);
        }
        item.source = relative;
        item.payload = rule.payload_dir.empty() ? (disc_dir / relative).string() : (rule.payload_dir / item.name).string();

        const int name_err = check_asset_name(item.name, error);
        if (name_err != RV_OK) {
            error = std::string(section) + ": " + error + " (from '" + relative + "')";
            return name_err;
        }

        plan.items.push_back(item);
    }
    return RV_OK;
}

// Append one block of the archive: its paths, in order, starting where the plan ended.
//
// @param relatives  paths relative to disc_dir, already split out of one glob
// @param section    section name for diagnostics, e.g. "[assets] files"
// @param disc_dir   directory the paths resolve against
// @param rule       how the entries are written
// @param plan       receives the entries, appended in order
// @param count      set to how many entries the block holds
// @param error      set with the section name already prefixed
// @return RV_OK on success, the RV_ERR_* of the refusing step otherwise
static int plan_block(const std::vector<std::string> &relatives,
    const char *section,
    const fs::path &disc_dir,
    const entry_rule &rule,
    archive_plan &plan,
    std::size_t &count,
    std::string &error)
{
    const std::size_t first = plan.items.size();

    const int append_err = append_entries(relatives, section, disc_dir, rule, plan, error);
    if (append_err != RV_OK) {
        return append_err;
    }

    count = plan.items.size() - first;
    return RV_OK;
}

// Plan the stream music: each [assets.stream] file is a sound, baked the same way.
//
// @param assets    every [assets] file, relative to disc_dir
// @param streams   the [assets.stream] files, relative to disc_dir
// @param disc_dir  directory the paths resolve against
// @param rule      how the entries are written
// @param plan      receives the entries, appended in order
// @param count     set to how many entries the block holds
// @param error     set with the reason and the section
// @return RV_OK on success, RV_ERR_INVAL for a refused file, the RV_ERR_* of planning otherwise
static int plan_stream_block(const std::vector<std::string> &assets,
    const std::vector<std::string> &streams,
    const fs::path &disc_dir,
    const entry_rule &rule,
    archive_plan &plan,
    std::size_t &count,
    std::string &error)
{
    for (const std::string &relative : streams) {
        if (std::find(assets.begin(), assets.end(), relative) != assets.end()) {
            error = "'" + relative + "' is listed in both \"" + k_assets_section + "\" and \"" + k_stream_section + "\"";
            return RV_ERR_INVAL;
        }
        if (rv_pdklib::rv_manifest_asset_kind_of(relative) != rv_pdklib::rv_manifest_asset_kind::sound) {
            error = std::string(k_stream_section) + ": '" + relative + "' is not a .wav or .mp3 file";
            return RV_ERR_INVAL;
        }
    }
    return plan_block(streams, k_stream_section, disc_dir, rule, plan, count, error);
}

} // namespace rv_pdktools

int rv_pdktools::plan_archive(const rv_pdklib::rv_manifest &manifest,
    const fs::path &disc_dir,
    const fs::path &texture_dir,
    const fs::path &scripts_dir,
    const fs::path &sound_dir,
    archive_plan &out,
    std::string &error)
{
    archive_plan plan;

    // --- [assets]: one glob, then each file goes to the block of its kind ---

    std::vector<std::string> assets;
    const int assets_err = glob_section(manifest.assets_files, k_assets_section, disc_dir, assets, error);
    if (assets_err != RV_OK) {
        return assets_err;
    }

    std::array<std::vector<std::string>, k_asset_kind_count> by_kind;
    for (const std::string &relative : assets) {
        by_kind[kind_slot(rv_pdklib::rv_manifest_asset_kind_of(relative))].push_back(relative);
    }
    const std::vector<std::string> &copies = by_kind[kind_slot(rv_pdklib::rv_manifest_asset_kind::copy)];
    const std::vector<std::string> &textures = by_kind[kind_slot(rv_pdklib::rv_manifest_asset_kind::texture)];
    const std::vector<std::string> &sounds = by_kind[kind_slot(rv_pdklib::rv_manifest_asset_kind::sound)];

    // --- copied assets ---

    const entry_rule copy_rule{ fs::path(), nullptr, nullptr };
    const int copy_err = plan_block(copies, k_assets_section, disc_dir, copy_rule, plan, plan.asset_count, error);
    if (copy_err != RV_OK) {
        return copy_err;
    }

    // --- textures ---

    const entry_rule texture_rule{ texture_dir, ".mppctex", nullptr };
    plan.first_texture = plan.items.size();
    const int textures_err = plan_block(textures, k_assets_section, disc_dir, texture_rule, plan, plan.texture_count, error);
    if (textures_err != RV_OK) {
        return textures_err;
    }

    // --- scripts ---

    const entry_rule script_rule{ scripts_dir, ".luac", nullptr };
    plan.first_script = plan.items.size();
    std::vector<std::string> scripts;
    const int scripts_glob_err = glob_section(manifest.scripts_sources, "[scripts] sources", disc_dir, scripts, error);
    if (scripts_glob_err != RV_OK) {
        return scripts_glob_err;
    }
    const int scripts_err = plan_block(scripts, "[scripts] sources", disc_dir, script_rule, plan, plan.script_count, error);
    if (scripts_err != RV_OK) {
        return scripts_err;
    }

    // --- sounds ---

    const entry_rule sound_rule{ sound_dir, ".pcm", nullptr };
    plan.first_sound = plan.items.size();
    const int sounds_err = plan_block(sounds, k_assets_section, disc_dir, sound_rule, plan, plan.sound_count, error);
    if (sounds_err != RV_OK) {
        return sounds_err;
    }

    // --- stream music ---
    //
    // Listed only in [assets.stream] files; baked to .pcm in the sound directory.
    std::vector<std::string> streams;
    const int streams_glob_err = glob_section(manifest.assets_stream_files, k_stream_section, disc_dir, streams, error);
    if (streams_glob_err != RV_OK) {
        return streams_glob_err;
    }
    plan.first_stream = plan.items.size();
    const int stream_err = plan_stream_block(assets, streams, disc_dir, sound_rule, plan, plan.stream_count, error);
    if (stream_err != RV_OK) {
        return stream_err;
    }

    // --- the whole plan at once ---
    //
    // Across sections, not within one: a PNG and a .lua can collapse onto the
    // same archive name just as easily as two PNGs can.
    const int collision_err = check_collisions(plan.items, error);
    if (collision_err != RV_OK) {
        return collision_err;
    }

    // Models need the model directory, which only plan_models() has; they wait here.
    plan.asset_models = std::move(by_kind[kind_slot(rv_pdklib::rv_manifest_asset_kind::model)]);

    out = std::move(plan);
    return RV_OK;
}

int rv_pdktools::plan_models(const fs::path &disc_dir, const fs::path &model_dir, archive_plan &plan, std::string &error)
{
    archive_plan next = plan;

    const entry_rule model_rule{ model_dir, nullptr, rv_pdklib::rv_mppcmod_disc_name };
    next.first_model = next.items.size();
    const int models_err = plan_block(next.asset_models, k_assets_section, disc_dir, model_rule, next, next.model_count, error);
    if (models_err != RV_OK) {
        return models_err;
    }

    // The [assets] models are placed now; a second call must not place them again.
    next.asset_models.clear();

    const int collision_err = check_collisions(next.items, error);
    if (collision_err != RV_OK) {
        return collision_err;
    }

    plan = std::move(next);
    return RV_OK;
}
