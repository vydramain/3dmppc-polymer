#include "rv_burner_burn.hpp"

#include <filesystem>
#include <string>
#include <system_error>

#include "rv_burner_zip/rv_burner_zipwrite.hpp"
#include "rv_burner_manifest/rv_burner_manifest.hpp"

namespace fs = std::filesystem;

namespace rv_pdktools
{

// The two names the loader reads before it looks at the asset namespace. They
// are refused as asset names by check_asset_name() for exactly this reason.
static constexpr const char *k_entry_manifest = "disc.toml";
static constexpr const char *k_entry_module = "disc.so";

} // namespace rv_pdktools

int rv_pdktools::burn_archive(
    const fs::path &output_path,
    const rv_burner_manifest &manifest,
    const fs::path &disc_module,
    const archive_plan &plan,
    int64_t &burned_size,
    std::string &error)
{
    std::error_code ec;
    fs::create_directories(output_path.parent_path(), ec);

    rv_zipwriter writer(output_path.string());
    if (!writer.ok()) {
        error = "cannot open '" + output_path.string() + "' for writing";
        return 1;
    }

    // --- the manifest ---

    const std::string manifest_text = manifest_render(manifest);
    if (!writer.add(k_entry_manifest, manifest_text.data(), manifest_text.size(), error)) {
        return 1;
    }

    // --- the module ---

    if (!writer.add_file(k_entry_module, disc_module.string(), error)) {
        return 1;
    }

    // --- everything the plan named ---

    for (const archive_item &item : plan.items) {
        if (!writer.add_file(item.name, item.payload, error)) {
            return 1;
        }
    }

    // Not optional: an archive without a central directory is not an archive,
    // and no reader will open it.
    if (!writer.finish(error)) {
        return 1;
    }

    burned_size = static_cast<int64_t>(fs::file_size(output_path, ec));
    if (ec) {
        burned_size = 0;
    }

    return 0;
}
