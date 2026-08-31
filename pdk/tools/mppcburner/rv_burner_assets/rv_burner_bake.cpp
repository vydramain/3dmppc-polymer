#include "rv_burner_bake.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>

#include "pdklib/rv_textures/rv_mppctex.hpp"
#include "rv_burner_common/rv_burner_bytes.hpp"
#include "pdklib/rv_textures/rv_texfmt_name.hpp"
#include "rv_burner_assets/rv_burner_baker_path.hpp"
#include "rv_burner_common/rv_burner_process.hpp"

namespace fs = std::filesystem;

namespace rv_pdktools
{

// Read back the header mppcbaker just wrote, refusing anything this burner would
// misparse. Each check answers a different question about the file: is it long
// enough to hold a header at all, is it a .mppctex, is it a layout we know, and
// does it claim a format the console implements.
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

    unsigned char raw[rv_pdklib::rv_mppctex_header_size] = {};
    file.read(reinterpret_cast<char *>(raw), sizeof(raw));
    if (file.gcount() != static_cast<std::streamsize>(sizeof(raw))) {
        error = "baked texture '" + path.string() + "' is shorter than its own header";
        return false;
    }

    if (std::memcmp(raw + rv_pdklib::RV_MPPCTEX_OFF_MAGIC,
            rv_pdklib::rv_mppctex_magic, sizeof(rv_pdklib::rv_mppctex_magic)) != 0) {
        error = "baked texture '" + path.string() + "' has no MPTX magic";
        return false;
    }

    const uint16_t version = read_le_u16(raw + rv_pdklib::RV_MPPCTEX_OFF_VERSION);
    if (version != rv_pdklib::rv_mppctex_version) {
        error = "baked texture '" + path.string() + "' is container version " +
            std::to_string(version) + ", this burner reads version " +
            std::to_string(rv_pdklib::rv_mppctex_version) +
            "; mppcbaker and mppcburner are out of step";
        return false;
    }

    const uint16_t format = read_le_u16(raw + rv_pdklib::RV_MPPCTEX_OFF_FORMAT);
    if (rv_pdklib::rv_texfmt_name::by_format(static_cast<rv_pdk::rv_texfmt>(format)) == nullptr) {
        error = "baked texture '" + path.string() + "' claims unknown format " +
            std::to_string(format);
        return false;
    }

    out.format = static_cast<rv_pdk::rv_texfmt>(format);
    out.width = read_le_u16(raw + rv_pdklib::RV_MPPCTEX_OFF_WIDTH);
    out.height = read_le_u16(raw + rv_pdklib::RV_MPPCTEX_OFF_HEIGHT);
    out.palette_count = read_le_u16(raw + rv_pdklib::RV_MPPCTEX_OFF_PALETTE_COUNT);
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
