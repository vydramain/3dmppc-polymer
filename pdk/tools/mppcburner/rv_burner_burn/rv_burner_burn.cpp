#include "rv_burner_burn.hpp"

#include <filesystem>
#include <fstream>
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

// Mirrors RV_PCLOADER_DEFAULT_CODE_ENTRY in src/rv_pconsole/rv_pcloader.cpp - the console
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
    // disc_module - only the name it gets inside the archive changes.

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

namespace rv_pdktools
{

// --- publishing one directory entry ---
//
// Every entry lands at a ".tmp" name next to its final name and is rename()'d
// into place. rename() within one directory is a single filesystem operation,
// so a reader can never observe a partially written file at the final name -
// it either is not there yet, or it is the complete thing. That guarantee is
// per entry, not for the directory as a whole: nothing here makes the set of
// entries atomic together.

static fs::path temp_sibling(const fs::path &dest)
{
    return dest.parent_path() / (dest.filename().string() + ".tmp");
}

// Renames `tmp` onto `dest`, replacing whatever was already there (a stale
// file, or nothing). On failure `tmp` is removed so a rejected build leaves no
// stray ".tmp" file behind.
static int finish_publish(const fs::path &tmp, const fs::path &dest, std::string &error)
{
    std::error_code ec;
    fs::rename(tmp, dest, ec);
    if (ec) {
        error = "cannot publish '" + dest.string() + "': " + ec.message();
        fs::remove(tmp, ec);
        return 1;
    }
    return 0;
}

// "disc.toml": the only entry whose bytes do not already exist as a file.
static int publish_text(const fs::path &dest, const std::string &text, std::string &error)
{
    const fs::path tmp = temp_sibling(dest);

    std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
    if (!out) {
        error = "cannot open '" + tmp.string() + "' for writing";
        return 1;
    }
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
    out.close();
    if (!out) {
        error = "short write on '" + tmp.string() + "'";
        std::error_code ec;
        fs::remove(tmp, ec);
        return 1;
    }

    return finish_publish(tmp, dest, error);
}

// A finished product: disc.so and a baked .mppctex. Nothing points back at
// these once written, so a copy is the honest relationship - a symlink to a
// PNG would not even be the right BYTES for a baked texture.
static int publish_copy(const fs::path &dest, const fs::path &source, std::string &error)
{
    const fs::path tmp = temp_sibling(dest);

    std::error_code ec;
    fs::remove(tmp, ec); // a stale leftover from an earlier failed run, if any
    fs::copy_file(source, tmp, fs::copy_options::overwrite_existing, ec);
    if (ec) {
        error = "cannot copy '" + source.string() + "' to '" + dest.string() + "': " + ec.message();
        return 1;
    }

    return finish_publish(tmp, dest, error);
}

// A script or a verbatim-copied asset: the developer's own file, which must
// stay editable through the directory. `source` is required to already be
// absolute - a relative link computed against this staging directory would
// dangle the moment the directory is read from anywhere else.
static int publish_link(const fs::path &dest, const fs::path &source, std::string &error)
{
    const fs::path tmp = temp_sibling(dest);

    std::error_code ec;
    fs::remove(tmp, ec);
    fs::create_symlink(source, tmp, ec);
    if (!ec) {
        rv_pdklib::rv_fprintf(stderr, "%s: symlinked to %s\n",
            dest.filename().string().c_str(), source.string().c_str());
    } else {
        ec.clear();
        fs::copy_file(source, tmp, fs::copy_options::overwrite_existing, ec);
        if (ec) {
            error = "cannot link or copy '" + source.string() + "' to '" + dest.string() + "': " +
                ec.message();
            return 1;
        }
        rv_pdklib::rv_fprintf(stderr, "%s: copied (symlink not available) from %s\n",
            dest.filename().string().c_str(), source.string().c_str());
    }

    return finish_publish(tmp, dest, error);
}

} // namespace rv_pdktools

int rv_pdktools::burn_directory(
    const fs::path &output_dir,
    const rv_pdklib::rv_manifest &manifest,
    const fs::path &disc_module,
    const archive_plan &plan,
    std::string &error)
{
    std::error_code ec;
    fs::create_directories(output_dir, ec);
    if (ec) {
        error = "cannot create '" + output_dir.string() + "'";
        return 1;
    }

    // --- the manifest ---

    const std::string manifest_text = rv_pdklib::rv_manifest_render(manifest);
    if (publish_text(output_dir / k_entry_manifest, manifest_text, error) != 0) {
        return 1;
    }

    // --- the module ---

    const std::string entry_module = manifest.budget.pccd.code_entry.empty() ? k_default_entry_module : manifest.budget.pccd.code_entry;

    if (publish_copy(output_dir / entry_module, disc_module, error) != 0) {
        return 1;
    }
    rv_pdklib::rv_fprintf(stderr, "disc module entry: %s\n", entry_module.c_str());

    // --- everything the plan named ---
    //
    // Textures are finished products and are copied; everything else (copied
    // assets and, for an unpacked build, scripts) is the developer's own file
    // and is symlinked so an edit stays visible.

    for (std::size_t i = 0; i < plan.items.size(); ++i) {
        const archive_item &item = plan.items[i];
        const bool is_texture = i >= plan.first_texture && i < plan.first_texture + plan.texture_count;
        const fs::path dest = output_dir / item.name;

        const int rc = is_texture
            ? publish_copy(dest, item.payload, error)
            : publish_link(dest, item.payload, error);
        if (rc != 0) {
            return 1;
        }
    }

    return 0;
}
