#include "rv_burner_build_runner.hpp"

#include <cstddef>
#include <filesystem>
#include <format>
#include <iostream>
#include <string>
#include <system_error>

#include "rv_burner_assets/rv_burner_bake.hpp"
#include "rv_burner_assets/rv_burner_plan.hpp"
#include "pdklib/rv_manifest/rv_manifest.hpp"
#include "rv_burner_print.hpp"

namespace fs = std::filesystem;

namespace rv_pdktools
{

// --- bake-texture: picking the one file out of [textures] ---
//
// Plans the whole archive the way build does (plan_archive) and picks the
// texture entry whose source resolves to `source`, so a texture's selection
// and its flat archive name can never drift from build's own rule
// (rv_burner_plan.cpp) - this does not restate either rule.
static int rv_burner_bake_texture_select(const rv_pdklib::rv_manifest &manifest, const fs::path &disc_dir,
    const std::string &source, archive_item &out, std::string &error)
{
    std::error_code ec;
    const fs::path source_arg(source);
    const fs::path candidate = source_arg.is_absolute() ? source_arg : disc_dir / source_arg;
    const fs::path canonical_source = fs::weakly_canonical(candidate, ec);
    if (ec) {
        error = std::format("'{}' does not exist", source);
        return 1;
    }

    // Payload dirs are never read from or written to here; only the plan's
    // names and sources are used, so a scratch directory that need not exist
    // is enough.
    const fs::path scratch = fs::temp_directory_path(ec);
    archive_plan plan;
    if (plan_archive(manifest, disc_dir, scratch, scratch, scratch, plan, error) != 0) {
        return 1;
    }

    for (std::size_t i = plan.first_texture; i < plan.first_texture + plan.texture_count; ++i) {
        const archive_item &item = plan.items[i];
        std::error_code item_ec;
        const fs::path item_canonical = fs::weakly_canonical(disc_dir / item.source, item_ec);
        if (!item_ec && item_canonical == canonical_source) {
            out = item;
            return 0;
        }
    }

    error = std::format("'{}' is not in [textures]", source);
    return 1;
}

} // namespace rv_pdktools

int rv_pdktools::rv_burner_bake_texture_run(const rv_burner_options &options)
{
    std::error_code ec;
    std::string error;

    const fs::path disc_dir = fs::weakly_canonical(fs::path(options.operand), ec);
    if (ec || !fs::is_directory(disc_dir, ec)) {
        rv_burner_print_error(std::format("'{}' is not a directory", options.operand));
        return 1;
    }

    rv_pdklib::rv_manifest manifest;
    if (rv_burner_build_manifest(disc_dir, manifest, error) != 0) {
        return 1;
    }

    archive_item item;
    if (rv_burner_bake_texture_select(manifest, disc_dir, options.bake_source, item, error) != 0) {
        rv_burner_print_error(error);
        return 1;
    }

    const fs::path output_path = fs::absolute(fs::path(options.output), ec);
    if (ec) {
        rv_burner_print_error(std::format("cannot resolve output path '{}'", options.output));
        return 1;
    }
    // Baked beside the final path and renamed into place only once mppcbaker
    // and the budget check both passed, so a refusal never leaves a partial
    // file at -o.
    const fs::path temp_path = output_path.string() + ".tmp";
    item.payload = temp_path.string();

    archive_plan plan;
    plan.items.push_back(item);
    plan.texture_count = 1;

    if (bake_textures(options.baker, manifest, disc_dir, plan, error) != 0) {
        fs::remove(temp_path, ec);
        rv_burner_print_error(error);
        return 1;
    }

    fs::rename(temp_path, output_path, ec);
    if (ec) {
        const std::string rename_error = ec.message();
        fs::remove(temp_path, ec);
        rv_burner_print_error(std::format("cannot write '{}': {}", output_path.string(), rename_error));
        return 1;
    }

    std::cout << item.name << "\n";
    return 0;
}
