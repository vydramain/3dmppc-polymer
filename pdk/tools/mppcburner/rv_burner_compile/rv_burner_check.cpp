#include "rv_burner_check.hpp"

#include <filesystem>
#include <system_error>

namespace fs = std::filesystem;

int rv_pdktools::check_sources_outside_disc(
    const rv_pdklib::rv_manifest &manifest,
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
            return 1;
        }

        const std::string resolved_text = resolved.string();
        const std::string disc_text = disc_dir.string();
        if (resolved_text.compare(0, disc_text.size(), disc_text) != 0) {
            error = "'" + include +
                "' points outside the disc directory. A disc may only include its own "
                "headers, the PDK and the SDK.";
            return 1;
        }

        absolute_includes.push_back(resolved_text);
    }

    return 0;
}
