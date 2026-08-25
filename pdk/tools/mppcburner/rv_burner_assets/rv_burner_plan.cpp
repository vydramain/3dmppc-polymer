#include "rv_burner_plan.hpp"

#include <filesystem>
#include <vector>

#include "rv_burner_common/rv_burner_globs.hpp"

namespace fs = std::filesystem;

namespace rv_pdktools
{

// Plan one manifest section into `plan.items`.
//
// The three sections differ in four things and nothing else, so they are four
// parameters rather than three copies of the same twenty lines: which patterns
// to expand, what the entry is called, where its bytes will come from, and how
// to say which section refused.
//
// @param patterns     the section's globs; an empty list plans nothing
// @param section      section name for diagnostics, e.g. "[textures] files"
// @param disc_dir     directory the globs resolve against
// @param payload_dir  where the produced file will be written; empty means the
//                     original file IS the payload and is archived as it is
// @param extension    replaces the original suffix, e.g. ".luac"; nullptr keeps
//                     the original file name unchanged
// @param plan         receives the entries, appended in order
// @param first        set to the index the first appended entry landed at
// @param count        set to how many entries were appended
// @param error        set with the section name already prefixed
// @return 0 on success, 1 on refusal
static int plan_section(
    const std::vector<std::string> &patterns,
    const char *section,
    const fs::path &disc_dir,
    const fs::path &payload_dir,
    const char *extension,
    archive_plan &plan,
    std::size_t &first,
    std::size_t &count,
    std::string &error)
{
    first = plan.items.size();
    count = 0;

    if (patterns.empty()) {
        return 0;
    }

    std::vector<std::string> matched;
    if (!glob_expand(disc_dir, patterns, matched, error)) {
        error = std::string(section) + ": " + error;
        return 1;
    }

    for (const std::string &relative : matched) {
        archive_item item;

        // A produced file keeps the author's file name and changes only its
        // suffix, so `ui/menu.lua` and `menu.luac` are recognisably the same
        // thing in a diagnostic. A copied file keeps its name entirely.
        item.name = extension != nullptr
            ? fs::path(relative).stem().string() + extension
            : flat_name(relative);
        item.source = relative;
        item.payload = payload_dir.empty()
            ? (disc_dir / relative).string()
            : (payload_dir / item.name).string();

        if (!check_asset_name(item.name, error)) {
            error = std::string(section) + ": " + error + " (from '" + relative + "')";
            return 1;
        }

        plan.items.push_back(item);
    }

    count = matched.size();
    return 0;
}

} // namespace rv_pdktools

int rv_pdktools::plan_archive(
    const rv_burner_manifest &manifest,
    const fs::path &disc_dir,
    const fs::path &texture_dir,
    const fs::path &scripts_dir,
    archive_plan &out,
    std::string &error)
{
    archive_plan plan;
    std::size_t first_asset = 0;

    // --- copied assets ---

    if (plan_section(manifest.assets_files, "[assets] files", disc_dir, fs::path(), nullptr,
            plan, first_asset, plan.asset_count, error) != 0) {
        return 1;
    }

    // --- textures ---

    if (plan_section(manifest.textures_files.files, "[textures] files", disc_dir, texture_dir,
            ".mppctex", plan, plan.first_texture, plan.texture_count, error) != 0) {
        return 1;
    }

    // --- scripts ---

    if (plan_section(manifest.scripts_sources, "[scripts] sources", disc_dir, scripts_dir,
            ".luac", plan, plan.first_script, plan.script_count, error) != 0) {
        return 1;
    }

    // --- the whole plan at once ---
    //
    // Across sections, not within one: a PNG and a .lua can collapse onto the
    // same archive name just as easily as two PNGs can.
    if (!check_collisions(plan.items, error)) {
        return 1;
    }

    out = std::move(plan);
    return 0;
}
