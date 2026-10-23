#include "rv_burner_build_runner.hpp"

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <format>
#include <iostream>
#include <string>
#include <system_error>

#include "rv_burner_assets/rv_burner_bake.hpp"
#include "rv_burner_assets/rv_burner_plan.hpp"
#include "pdk/rv_err.h"
#include "pdklib/rv_manifest/rv_manifest.hpp"
#include "rv_burner_print.hpp"

namespace fs = std::filesystem;

namespace rv_pdktools
{

// --- bake-texture: picking the one file out of the [assets] textures ---
//
// Plans the whole archive the way build does (plan_archive) and picks the
// texture entry whose source resolves to `source`, so a texture's selection
// and its flat archive name can never drift from build's own rule
// (rv_burner_plan.cpp) - this does not restate either rule.
static int rv_burner_bake_texture_select(const rv_pdklib::rv_manifest &manifest,
    const fs::path &disc_dir,
    const std::string &source,
    archive_item &out,
    std::string &error)
{
    std::error_code ec;
    const fs::path source_arg(source);
    const fs::path candidate = source_arg.is_absolute() ? source_arg : disc_dir / source_arg;
    const fs::path canonical_source = fs::weakly_canonical(candidate, ec);
    if (ec) {
        error = std::format("'{}' does not exist", source);
        return RV_ERR_NOENT;
    }

    // Payload dirs are never read from or written to here; only the plan's
    // names and sources are used, so a scratch directory that need not exist
    // is enough.
    const fs::path scratch = fs::temp_directory_path(ec);
    archive_plan plan;
    const int plan_err = plan_archive(manifest, disc_dir, scratch, scratch, scratch, plan, error);
    if (plan_err != RV_OK) {
        return plan_err;
    }

    const auto first = plan.items.begin() + static_cast<std::ptrdiff_t>(plan.first_texture);
    const auto last = first + static_cast<std::ptrdiff_t>(plan.texture_count);
    const auto found = std::find_if(first, last, [&](const archive_item &item) {
        std::error_code item_ec;
        const fs::path item_canonical = fs::weakly_canonical(disc_dir / item.source, item_ec);
        return !item_ec && item_canonical == canonical_source;
    });
    if (found == last) {
        error = std::format("'{}' is not a texture in [assets]", source);
        return RV_ERR_NOENT;
    }

    out = *found;
    return RV_OK;
}

} // namespace rv_pdktools

int rv_pdktools::rv_burner_bake_texture_run(const rv_burner_options &options)
{
    std::error_code ec;
    std::string error;

    const fs::path disc_dir = fs::weakly_canonical(fs::path(options.operand), ec);
    if (ec || !fs::is_directory(disc_dir, ec)) {
        rv_burner_print_error(std::format("'{}' is not a directory", options.operand));
        return RV_ERR_NOENT;
    }

    rv_pdklib::rv_manifest manifest;
    const int manifest_err = rv_burner_build_manifest(disc_dir, manifest, error);
    if (manifest_err != RV_OK) {
        return manifest_err;
    }

    archive_item item;
    const int select_rv = rv_burner_bake_texture_select(manifest, disc_dir, options.bake_source, item, error);
    if (select_rv != RV_OK) {
        rv_burner_print_error(error);
        return select_rv;
    }

    const fs::path output_path = fs::absolute(fs::path(options.output), ec);
    if (ec) {
        rv_burner_print_error(std::format("cannot resolve output path '{}'", options.output));
        return RV_ERR_IO;
    }
    // Baked beside the final path and renamed into place only once mppctexbaker
    // and the budget check both passed, so a refusal never leaves a partial
    // file at -o.
    const fs::path temp_path = output_path.string() + ".tmp";
    item.payload = temp_path.string();

    archive_plan plan;
    plan.items.push_back(item);
    plan.texture_count = 1;

    const int bake_err = bake_textures(options.baker, manifest, disc_dir, plan, error);
    if (bake_err != RV_OK) {
        fs::remove(temp_path, ec);
        rv_burner_print_error(error);
        return bake_err;
    }

    fs::rename(temp_path, output_path, ec);
    if (ec) {
        const std::string rename_error = ec.message();
        fs::remove(temp_path, ec);
        rv_burner_print_error(std::format("cannot write '{}': {}", output_path.string(), rename_error));
        return RV_ERR_IO;
    }

    std::cout << item.name << "\n";
    return RV_OK;
}
