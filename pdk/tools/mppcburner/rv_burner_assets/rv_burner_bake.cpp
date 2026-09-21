#include "rv_burner_bake.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

#include "pdklib/rv_textures/rv_mppctex.hpp"
#include "pdklib/rv_textures/rv_texfmt_name.hpp"
#include "rv_burner_assets/rv_burner_baker_path.hpp"
#include "rv_burner_common/rv_burner_process.hpp"

namespace fs = std::filesystem;

namespace rv_pdktools
{

// Read back the WHOLE file mppcbaker just wrote and hand it to
// rv_pdklib::rv_mppctex_parse() - the one validator this burner shares with a
// disc's own loader, so a texture accepted here is a texture the console can
// actually upload later. This burner used to check only the header's first
// four fields, which is how a zero-dimension or truncated .mppctex used to
// pass here and only fail once a disc tried to load it; that gap is closed by
// asking the same question the console asks, not a looser one of our own.
static bool read_mppctex_header(
    const fs::path &path,
    rv_pdklib::rv_mppctex_header &out,
    std::string &error)
{
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        error = "cannot reopen baked texture '" + path.string() + "'";
        return false;
    }

    std::error_code ec;
    const uintmax_t size = fs::file_size(path, ec);
    if (ec) {
        error = "cannot size baked texture '" + path.string() + "'";
        return false;
    }

    std::vector<std::byte> bytes(static_cast<size_t>(size));
    file.read(reinterpret_cast<char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (file.gcount() != static_cast<std::streamsize>(bytes.size())) {
        error = "baked texture '" + path.string() + "' is shorter than its own reported size";
        return false;
    }

    const std::byte *palette = nullptr;
    const std::byte *texels = nullptr;
    std::string reason;
    if (!rv_pdklib::rv_mppctex_parse(bytes, out, palette, texels, reason)) {
        error = "baked texture '" + path.string() + "' " + reason;
        return false;
    }
    return true;
}

} // namespace rv_pdktools

int rv_pdktools::bake_textures(
    const std::string &baker_hint,
    const rv_pdklib::rv_manifest &manifest,
    const fs::path &disc_dir,
    const archive_plan &plan,
    std::string &error)
{
    if (plan.texture_count == 0) {
        return 0;
    }

    std::string baker;
    if (!find_baker(baker_hint, baker, error)) {
        return 1;
    }

    // One format for the whole manifest, so the spelling mppcbaker is given is
    // looked up once rather than per texture. The accepted spellings are the
    // rows of rv_texfmt_names and are not restated in this tool.
    const rv_pdklib::rv_texfmt_name *texfmt =
        rv_pdklib::rv_texfmt_name::by_format(manifest.textures_files.format);
    const std::string texfmt_text = texfmt != nullptr ? texfmt->text : "";

    for (std::size_t i = plan.first_texture; i < plan.first_texture + plan.texture_count; ++i) {
        const archive_item &item = plan.items[i];

        // --- run mppcbaker ---

        const std::string command = shell_quote(baker) + " " +
            shell_quote((disc_dir / item.source).string()) + " " +
            shell_quote(item.payload) + " --format " + texfmt_text;

        std::string child_output;
        const int status = run_capture(command, child_output);
        if (status != 0) {
            dump_child_output(child_output);
            error = "mppcbaker failed on '" + item.source + "' (exit " +
                std::to_string(status) + ")";
            return 1;
        }

        // --- read back what it produced ---

        rv_pdklib::rv_mppctex_header header;
        if (!read_mppctex_header(item.payload, header, error)) {
            return 1;
        }

        // --- one texture against the machine's limits ---

        if (header.width > manifest.budget.pccv.texture_max_width ||
            header.height > manifest.budget.pccv.texture_max_height) {
            error = "texture '" + item.source + "' is " + std::to_string(header.width) + "x" +
                std::to_string(header.height) + ", over the budget of " +
                std::to_string(manifest.budget.pccv.texture_max_width) + "x" +
                std::to_string(manifest.budget.pccv.texture_max_height) + " declared in [budget]";
            return 1;
        }
    }

    return 0;
}
