#include "rv_burner_bake.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

#include "pdk/rv_err.h"
#include "pdklib/rv_textures/rv_mppctex.hpp"
#include "pdklib/rv_textures/rv_texfmt_name.hpp"
#include "rv_burner_assets/rv_burner_baker_path.hpp"
#include "rv_burner_common/rv_burner_process.hpp"

namespace fs = std::filesystem;

namespace rv_pdktools
{

// The sound baker's program name; the texture baker's is in rv_burner_baker_path.hpp.
static constexpr const char *k_sound_baker = "mppcmusbaker";

// S16 sample is 16 bits = 2 bytes; PCM size must be a whole multiple.
static constexpr std::size_t k_pcm_sample_bytes = 2;

// Read back the WHOLE file mppctexbaker just wrote and hand it to
// rv_pdklib::rv_mppctex_parse() - the one validator this burner shares with a
// disc's own loader, so a texture accepted here is a texture the console can
// actually upload later. This burner used to check only the header's first
// four fields, which is how a zero-dimension or truncated .mppctex used to
// pass here and only fail once a disc tried to load it; that gap is closed by
// asking the same question the console asks, not a looser one of our own.
static int read_mppctex_header(const fs::path &path, rv_pdklib::rv_mppctex_header &out, std::string &error)
{
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        error = "cannot reopen baked texture '" + path.string() + "'";
        return RV_ERR_IO;
    }

    std::error_code ec;
    const uintmax_t size = fs::file_size(path, ec);
    if (ec) {
        error = "cannot size baked texture '" + path.string() + "'";
        return RV_ERR_IO;
    }

    std::vector<std::byte> bytes(static_cast<size_t>(size));
    file.read(reinterpret_cast<char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (file.gcount() != static_cast<std::streamsize>(bytes.size())) {
        error = "baked texture '" + path.string() + "' is shorter than its own reported size";
        return RV_ERR_IO;
    }

    const std::byte *palette = nullptr;
    const std::byte *texels = nullptr;
    std::string reason;
    if (rv_pdklib::rv_mppctex_parse(bytes, out, palette, texels, reason) != RV_OK) {
        error = "baked texture '" + path.string() + "' " + reason;
        return RV_ERR_INVAL;
    }
    return RV_OK;
}

// Sounds have no header to parse: the only shape a .pcm must have is an even
// byte count, since it holds S16 samples.
static int check_pcm_size(const fs::path &path, std::string &error)
{
    std::error_code ec;
    const uintmax_t size = fs::file_size(path, ec);
    if (ec || size == 0) {
        error = "baked sound '" + path.string() + "' is empty";
        return RV_ERR_IO;
    }
    if (size % k_pcm_sample_bytes != 0) {
        error = "baked sound '" + path.string() + "' is not a whole number of S16 samples";
        return RV_ERR_INVAL;
    }
    return RV_OK;
}

} // namespace rv_pdktools

int rv_pdktools::bake_one_texture(const std::string &baker,
    const fs::path &source,
    const fs::path &payload,
    const rv_pdklib::rv_manifest &manifest,
    const std::string &label,
    std::string &error)
{
    // The accepted --format spellings are the rows of rv_texfmt_names and are not restated here.
    const rv_pdklib::rv_texfmt_name *texfmt = rv_pdklib::rv_texfmt_name::by_format(manifest.assets_texture_format);
    const std::string texfmt_text = texfmt != nullptr ? texfmt->text : "";

    // --- run mppctexbaker ---

    const std::string command = shell_quote(baker) + " " + shell_quote(source.string()) + " " + shell_quote(payload.string()) +
        " --format " + texfmt_text;

    std::string child_output;
    const int status = run_capture(command, child_output);
    if (status != 0) {
        dump_child_output(child_output);
        error = std::string(k_texture_baker) + " failed on '" + label + "' (exit " + std::to_string(status) + ")";
        return RV_ERR_IO;
    }

    // --- read back what it produced ---

    rv_pdklib::rv_mppctex_header header;
    const int read_err = read_mppctex_header(payload, header, error);
    if (read_err != RV_OK) {
        return read_err;
    }

    // --- one texture against the machine's limits ---

    if (header.width > manifest.budget.pccv.texture_max_width || header.height > manifest.budget.pccv.texture_max_height) {
        error = "texture '" + label + "' is " + std::to_string(header.width) + "x" + std::to_string(header.height) +
            ", over the budget of " + std::to_string(manifest.budget.pccv.texture_max_width) + "x" +
            std::to_string(manifest.budget.pccv.texture_max_height) + " declared in [budget]";
        return RV_ERR_INVAL;
    }
    return RV_OK;
}

int rv_pdktools::bake_textures(const std::string &baker_hint,
    const rv_pdklib::rv_manifest &manifest,
    const fs::path &disc_dir,
    const archive_plan &plan,
    std::string &error)
{
    // An empty texture range needs no baker, and the loop below then does nothing.
    std::string baker;
    const int baker_err = plan.texture_count == 0 ? RV_OK : find_baker(baker_hint, k_texture_baker, baker, error);
    if (baker_err != RV_OK) {
        return baker_err;
    }

    for (std::size_t i = plan.first_texture; i < plan.first_texture + plan.texture_count; ++i) {
        const archive_item &item = plan.items[i];
        const int bake_err = bake_one_texture(baker, disc_dir / item.source, item.payload, manifest, item.source, error);
        if (bake_err != RV_OK) {
            return bake_err;
        }
    }

    return RV_OK;
}

int rv_pdktools::bake_sounds(const std::string &baker_hint,
    const fs::path &disc_dir,
    const archive_plan &plan,
    std::string &error)
{
    // An empty sound range needs no baker, and the loop below then does nothing.
    std::string baker;
    const int baker_err = plan.sound_count == 0 ? RV_OK : find_baker(baker_hint, k_sound_baker, baker, error);
    if (baker_err != RV_OK) {
        return baker_err;
    }

    for (std::size_t i = plan.first_sound; i < plan.first_sound + plan.sound_count; ++i) {
        const archive_item &item = plan.items[i];

        const std::string command =
            shell_quote(baker) + " " + shell_quote((disc_dir / item.source).string()) + " " + shell_quote(item.payload);

        std::string child_output;
        const int status = run_capture(command, child_output);
        if (status != 0) {
            dump_child_output(child_output);
            error = std::string(k_sound_baker) + " failed on '" + item.source + "' (exit " + std::to_string(status) + ")";
            return RV_ERR_IO;
        }

        const int pcm_err = check_pcm_size(item.payload, error);
        if (pcm_err != RV_OK) {
            return pcm_err;
        }
    }

    return RV_OK;
}
