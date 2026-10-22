#include "rv_burner_check.hpp"

#include <filesystem>
#include <system_error>

#include "pdk/rv_err.h"

namespace fs = std::filesystem;

int rv_pdktools::check_sources_outside_disc(const rv_pdklib::rv_manifest &manifest,
    const fs::path &disc_dir,
    const std::vector<std::string> &sources,
    std::vector<std::string> &absolute_includes,
    std::vector<std::string> &absolute_sources,
    std::string &error)
{
    std::error_code ec;

    // --- sources ---

    absolute_sources.reserve(sources.size());
    for (const std::string &source : sources) {
        absolute_sources.push_back((disc_dir / source).string());
    }

    // --- include directories ---
    //
    // weakly_canonical resolves `..` and symlinks, so a directory that only
    // LOOKS like it is inside the disc cannot slip through the prefix test
    // below. Refusing here is what keeps the "no src/" rule true whatever the
    // manifest asks for: the only paths outside the disc that ever reach the
    // generated project are the PDK and the SDK, and the burner puts those
    // there itself.
    for (const std::string &include : manifest.build_include_dirs) {
        const fs::path resolved = fs::weakly_canonical(disc_dir / include, ec);
        if (ec) {
            error = "cannot resolve '" + include + "': " + ec.message();
            return RV_ERR_IO;
        }

        const std::string resolved_text = resolved.string();
        const std::string disc_text = disc_dir.string();
        if (resolved_text.compare(0, disc_text.size(), disc_text) != 0) {
            error = "'" + include +
                "' points outside the disc directory. A disc may only include its own "
                "headers, the PDK and the SDK.";
            return RV_ERR_INVAL;
        }

        absolute_includes.push_back(resolved_text);
    }

    return RV_OK;
}

// Each SDK tree keeps its headers in <tree>/include, the same layout as the PDK.
static constexpr const char *k_sdk_include_subdir = "include";

int rv_pdktools::resolve_sdk_trees(const rv_pdklib::rv_manifest &manifest,
    const fs::path &pdk_include_dir,
    std::vector<std::string> &absolute_includes,
    std::string &error)
{
    if (manifest.build_sdk.empty()) {
        return RV_OK;
    }

    // The SDK trees sit beside the PDK. ".." instead of parent_path(): for a path that
    // does not exist, weakly_canonical keeps a trailing separator, which would leave
    // the root one level down.
    std::error_code ec;
    const fs::path pdk_root = fs::weakly_canonical(pdk_include_dir / "..", ec);
    if (ec) {
        error = "cannot resolve the PDK directory '" + pdk_include_dir.string() + "': " + ec.message();
        return RV_ERR_IO;
    }

    for (const std::string &name : manifest.build_sdk) {
        std::error_code dir_ec;
        const fs::path tree = pdk_root / name / k_sdk_include_subdir;
        // A missing tree, or a file where a directory on its path should be, is reported as an
        // error code by is_directory, not as a plain false.
        const bool is_dir = fs::is_directory(tree, dir_ec);
        const bool missing = dir_ec == std::errc::no_such_file_or_directory || dir_ec == std::errc::not_a_directory;
        if (dir_ec && !missing) {
            error = "cannot check SDK tree '" + tree.string() + "': " + dir_ec.message();
            return RV_ERR_IO;
        }
        if (!is_dir) {
            error =
                "'" + name + "' has no tree at '" + tree.string() + "'; add the tree there or remove '" + name + "' from sdk";
            return RV_ERR_NOENT;
        }

        absolute_includes.push_back(tree.string());
    }

    return RV_OK;
}
