#include "rv_burner_burn.hpp"

#include <filesystem>
#include <string>
#include <system_error>

#include "rv_burner_zip/rv_burner_zipwrite.hpp"
#include "pdklib/rv_manifest/rv_manifest.hpp"
#include "pdklib/rv_stdio/rv_stdio.hpp"

namespace fs = std::filesystem;

namespace rv_pdktools
{

// The two names the loader reads before it looks at the asset namespace. They
// are refused as asset names by check_asset_name() for exactly this reason.
static constexpr const char *k_entry_manifest = "disc.toml";

// Mirrors kDefaultCodeEntry in src/rv_pconsole/rv_pcloader.cpp — the console
// falls back to this same literal when the manifest leaves code_entry blank.
// Kept as a separate constant (not shared across the two trees) but named
// identically in spirit so the pair is easy to find.
static constexpr const char *k_default_entry_module = "disc.so";

} // namespace rv_pdktools

int rv_pdktools::burn_archive(
    const fs::path &output_path,
    const rv_pdklib::rv_manifest &manifest,
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

    const std::string manifest_text = rv_pdklib::rv_manifest_render(manifest);
    if (!writer.add(k_entry_manifest, manifest_text.data(), manifest_text.size(), error)) {
        return 1;
    }

    // --- the module ---
    //
    // The manifest names the entry the module is stored under; a blank value
    // means the conventional name. The file compiled to disk is always
    // disc_module — only the name it gets inside the archive changes.

    const std::string entry_module = manifest.budget.pccd.code_entry.empty() ? k_default_entry_module : manifest.budget.pccd.code_entry;

    if (!writer.add_file(entry_module, disc_module.string(), error)) {
        return 1;
    }
    rv_pdklib::rv_fprintf(stderr, "disc module entry: %s\n", entry_module.c_str());

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
