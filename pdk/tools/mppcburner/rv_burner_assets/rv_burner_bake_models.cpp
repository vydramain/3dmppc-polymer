#include "rv_burner_bake_models.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

#include "pdk/rv_err.h"
#include "pdklib/rv_models/rv_mppcmod.hpp"
#include "pdklib/rv_models/rv_mppcmod_int.hpp"
#include "rv_burner_assets/rv_burner_bake.hpp"
#include "rv_burner_assets/rv_burner_baker_path.hpp"
#include "rv_burner_common/rv_burner_process.hpp"

namespace fs = std::filesystem;

namespace rv_pdktools
{

// The program each model is baked by. Named once so the lookup and the error
// text cannot disagree about which program ran.
static constexpr const char *k_model_baker = "mppcmodbaker";

// Read back the WHOLE .mppcmod mppcmodbaker just wrote and hand it to
// rv_pdklib::rv_mppcmod_int_parse(). A partial check would let a truncated model
// pass here and fail only on a player's console.
static int read_mppcmod(const fs::path &path, rv_pdklib::rv_mppcmod_int_model &model, std::string &error)
{
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        error = "cannot reopen baked model '" + path.string() + "'";
        return RV_ERR_IO;
    }

    std::error_code ec;
    const std::uintmax_t size = fs::file_size(path, ec);
    if (ec) {
        error = "cannot size baked model '" + path.string() + "'";
        return RV_ERR_IO;
    }

    std::vector<std::byte> bytes(static_cast<std::size_t>(size));
    file.read(reinterpret_cast<char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (file.gcount() != static_cast<std::streamsize>(bytes.size())) {
        error = "baked model '" + path.string() + "' is shorter than its own reported size";
        return RV_ERR_IO;
    }

    std::string reason;
    if (rv_pdklib::rv_mppcmod_int_parse(bytes, model, reason) != RV_OK) {
        error = "baked model '" + path.string() + "' " + reason;
        return RV_ERR_INVAL;
    }
    return RV_OK;
}

// Adds the textures one model names, each name once across all models. A colour-only
// part names no texture and is skipped. The PNG is the one mppcmodbaker left beside the
// model under the same stem, so the .mppctex and its source follow from the name alone.
static void
collect_textures(const rv_pdklib::rv_mppcmod_int_model &model, const archive_item &item, std::vector<model_texture> &found)
{
    const fs::path model_dir = fs::path(item.payload).parent_path();
    for (const rv_pdklib::rv_mppcmod_part &part : model.parts) {
        if (part.texture.empty()) {
            continue;
        }
        const bool known = std::any_of(found.begin(), found.end(), [&part](const model_texture &t) {
            return t.name == part.texture;
        });
        if (known) {
            continue;
        }
        found.push_back({ part.texture,
            item.source + " texture " + part.texture,
            model_dir / (fs::path(part.texture).stem().string() + ".png") });
    }
}

} // namespace rv_pdktools

int rv_pdktools::bake_models(const std::string &baker_hint,
    const fs::path &disc_dir,
    const archive_plan &plan,
    std::vector<model_texture> &textures,
    std::string &error)
{
    textures.clear();

    // An empty model range needs no baker, and the loop below then does nothing.
    std::string baker;
    const int baker_err = plan.model_count == 0 ? RV_OK : find_baker(baker_hint, k_model_baker, baker, error);
    if (baker_err != RV_OK) {
        return baker_err;
    }

    for (std::size_t i = plan.first_model; i < plan.first_model + plan.model_count; ++i) {
        const archive_item &item = plan.items[i];

        // A model left by an earlier run must not pass for this run's output.
        std::error_code ec;
        fs::remove(item.payload, ec);
        if (ec) {
            error = "cannot remove the stale model '" + item.payload + "': " + ec.message();
            return RV_ERR_IO;
        }

        const std::string command =
            shell_quote(baker) + " " + shell_quote((disc_dir / item.source).string()) + " " + shell_quote(item.payload);

        std::string child_output;
        const int status = run_capture(command, child_output);
        if (status != 0) {
            dump_child_output(child_output);
            error = std::string(k_model_baker) + " failed on '" + item.source + "' (exit " + std::to_string(status) + ")";
            return RV_ERR_IO;
        }

        rv_pdklib::rv_mppcmod_int_model model;
        const int read_err = read_mppcmod(item.payload, model, error);
        if (read_err != RV_OK) {
            return read_err;
        }
        collect_textures(model, item, textures);
    }

    return RV_OK;
}

int rv_pdktools::plan_model_textures(const std::vector<model_texture> &textures,
    const fs::path &texture_dir,
    archive_plan &plan,
    std::string &error)
{
    archive_plan next = plan;
    next.first_model_texture = next.items.size();
    next.model_texture_count = textures.size();
    for (const model_texture &texture : textures) {
        const int name_err = check_asset_name(texture.name, error);
        if (name_err != RV_OK) {
            return name_err;
        }

        archive_item item;
        item.name = texture.name;
        item.source = texture.source;
        item.payload = (texture_dir / texture.name).string();
        next.items.push_back(item);
    }

    const int collision_err = check_collisions(next.items, error);
    if (collision_err != RV_OK) {
        return collision_err;
    }

    plan = std::move(next);
    return RV_OK;
}

int rv_pdktools::bake_model_textures(const std::string &baker_hint,
    const rv_pdklib::rv_manifest &manifest,
    const std::vector<model_texture> &textures,
    const archive_plan &plan,
    std::string &error)
{
    if (plan.model_texture_count != textures.size()) {
        error = "model textures were baked before they were planned";
        return RV_ERR_INVAL;
    }

    // A model without textures needs no texture baker, and the loop then does nothing.
    // The PNGs are mppcmodbaker's output, never edited by hand: baking them here keeps
    // each .mppctex matched to the model it was baked with.
    std::string texture_baker;
    const int baker_err = textures.empty() ? RV_OK : find_baker(baker_hint, k_texture_baker, texture_baker, error);
    if (baker_err != RV_OK) {
        return baker_err;
    }

    for (std::size_t i = 0; i < textures.size(); ++i) {
        const model_texture &texture = textures[i];
        const archive_item &item = plan.items[plan.first_model_texture + i];
        const int bake_err = bake_one_texture(texture_baker, texture.png, item.payload, manifest, texture.source, error);
        if (bake_err != RV_OK) {
            return bake_err;
        }
    }
    return RV_OK;
}
