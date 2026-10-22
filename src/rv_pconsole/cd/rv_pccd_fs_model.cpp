// rv_pccd_fs MODEL residency: a .mppcmod read from the disc, its textures made
// resident through the TEXTURE path, and the whole model uploaded to the
// geometry controller. Kept out of rv_pccd_fs.cpp, which is at its size limit.
#include <cmath>
#include <new>
#include <utility>
#include <string>
#include <vector>

#include "pdk/rv_err.h"
#include "pdklib/rv_logs/rv_logs.hpp"
#include "pdklib/rv_models/rv_mppcmod.hpp"
#include "pdklib/rv_models/rv_mppcmod_int.hpp"
#include "rv_pconsole/cd/rv_pccd_fs.hpp"
#include "rv_pconsole/cg/rv_pccg.hpp"

namespace rv_3dmppc
{

namespace
{
// 4.12 uv fixed point: 4096 is the whole texture.
constexpr float RV_PCCD_MODEL_UV_ONE = 4096.0f;

// Integer model -> float model, the input of the float renderer. This is the transition step: it goes
// away when the renderer takes the integer layout. Position is v / 2^shift and uv is u / 4096; both
// divisions are exact in float, so no rounding is added here.
rv_pdklib::rv_mppcmod int_to_float_model(const rv_pdklib::rv_mppcmod_int_model &in)
{
    rv_pdklib::rv_mppcmod out;
    out.parts = in.parts;
    out.triangles = in.triangles;
    out.vertices.resize(in.vertices.size());
    for (size_t v = 0; v < in.vertices.size(); ++v) {
        const rv_pdklib::rv_mppcmod_int_vertex &src = in.vertices[v];
        rv_pdklib::rv_mppcmod_vertex &dst = out.vertices[v];
        for (size_t k = 0; k < dst.pos.size(); ++k) {
            dst.pos[k] = std::ldexp(static_cast<float>(src.pos[k]), -static_cast<int>(in.shift));
        }
        for (size_t k = 0; k < dst.uv.size(); ++k) {
            dst.uv[k] = static_cast<float>(src.uv[k]) / RV_PCCD_MODEL_UV_ONE;
        }
        dst.color = src.color;
    }
    return out;
}
} // namespace

int64_t rv_pccd_fs::resident_model_(const char *resname, model_record *&model_out)
{
    // No geometry attached is the same situation as no video attached: nothing
    // can be made resident yet.
    if (cg_ == nullptr) {
        return RV_ERR_INVAL;
    }

    const std::string key(resname);
    const auto it = model_by_name_.find(key);
    if (it != model_by_name_.end()) {
        model_out = &models_[static_cast<size_t>(it->second)];
        return RV_OK;
    }
    return model_resolve_(resname, model_out);
}

int64_t rv_pccd_fs::model_resolve_(const char *resname, model_record *&record_out)
{
    std::vector<std::byte> bytes;
    const int64_t read_rc = asset_read_bytes_(resname, bytes);
    if (read_rc < 0) {
        return read_rc;
    }

    rv_pdklib::rv_mppcmod_int_model int_model;
    std::string error;
    if (rv_pdklib::rv_mppcmod_int_parse(bytes, int_model, error) != RV_OK) {
        RV_LOG_WARN("pccd", "model '{}' refused: it {}", rv_pdklib::rv_log_escape(resname), error);
        return RV_ERR_INVAL;
    }
    const rv_pdklib::rv_mppcmod model = int_to_float_model(int_model);

    // One entry per part, in part order, as model_upload() requires; a part
    // without a texture keeps addr_texture 0 and texture index -1.
    std::vector<rv_pccg_part_texture> textures(model.parts.size(), rv_pccg_part_texture{ 0, 0, 0, 0 });
    std::vector<int64_t> part_texture(model.parts.size(), -1);
    for (size_t p = 0; p < model.parts.size(); ++p) {
        const std::string &texture_name = model.parts[p].texture;
        if (texture_name.empty()) {
            continue;
        }
        texture_record *texture = nullptr;
        const int64_t texture_rc = resident_texture_(texture_name.c_str(), texture);
        if (texture_rc < 0) {
            RV_LOG_WARN("pccd",
                "model '{}' refused: part {} texture '{}' is not resident (rv_err {})",
                rv_pdklib::rv_log_escape(resname),
                p,
                rv_pdklib::rv_log_escape(texture_name.c_str()),
                texture_rc);
            return texture_rc;
        }
        textures[p] = { texture->tex_addr, texture->pal_addr, texture->width, texture->height };
        part_texture[p] = static_cast<int64_t>(texture - textures_.data());
    }

    const int64_t addr = cg_->model_upload(model, textures);
    if (addr < 0) {
        RV_LOG_WARN("pccd", "model '{}' not uploaded (rv_err {})", rv_pdklib::rv_log_escape(resname), addr);
        return addr;
    }

    model_record record;
    record.resname = resname;
    record.addr = addr;
    record.textures = std::move(textures);
    record.part_texture = std::move(part_texture);

    const int64_t index = static_cast<int64_t>(models_.size());
    try {
        std::string key = record.resname;
        models_.push_back(std::move(record));
        model_by_name_[std::move(key)] = index;
    } catch (const std::bad_alloc &) {
        cg_->model_free(addr);
        if (static_cast<int64_t>(models_.size()) > index) {
            models_.pop_back();
        }
        return RV_ERR_NOMEM;
    }

    record_out = &models_[static_cast<size_t>(index)];
    return RV_OK;
}

namespace
{
// A part reads texture `index` when it was uploaded from that texture record.
bool reads_texture(const std::vector<int64_t> &part_texture, int64_t index)
{
    for (const int64_t part : part_texture) {
        if (part == index) {
            return true;
        }
    }
    return false;
}

// Puts `value` into every part that reads texture `index`.
void set_parts(std::vector<rv_pccg_part_texture> &textures,
    const std::vector<int64_t> &part_texture,
    int64_t index,
    const rv_pccg_part_texture &value)
{
    for (size_t p = 0; p < textures.size(); ++p) {
        if (part_texture[p] == index) {
            textures[p] = value;
        }
    }
}
} // namespace

int64_t
rv_pccd_fs::models_retexture_(int64_t index, const rv_pccg_part_texture &from, const rv_pccg_part_texture &to, bool &stuck)
{
    stuck = false;
    for (size_t m = 0; m < models_.size(); ++m) {
        if (!reads_texture(models_[m].part_texture, index)) {
            continue;
        }
        const int64_t rc = model_retexture_(models_[m], index, to);
        if (rc < 0) {
            RV_LOG_WARN("pccd",
                "texture refresh refused: model '{}' keeps its old texture (rv_err {})",
                rv_pdklib::rv_log_escape(models_[m].resname.c_str()),
                rc);
            // Only the models before m were moved; they are the ones reading `index`.
            const int64_t revert_rc = models_revert_(index, from, m);
            stuck = revert_rc < 0;
            return revert_rc < 0 ? revert_rc : rc;
        }
    }
    return RV_OK;
}

int64_t rv_pccd_fs::models_revert_(int64_t index, const rv_pccg_part_texture &from, size_t moved)
{
    int64_t first = RV_OK;
    for (size_t m = 0; m < moved; ++m) {
        if (!reads_texture(models_[m].part_texture, index)) {
            continue;
        }
        const int64_t rc = model_retexture_(models_[m], index, from);
        if (rc < 0) {
            RV_LOG_ERR("pccd",
                "model '{}' keeps the new texture: it could not go back (rv_err {}); the new blocks belong to no "
                "texture record, so the drive never frees them and they stay in video RAM until the video controller "
                "is destroyed",
                rv_pdklib::rv_log_escape(models_[m].resname.c_str()),
                rc);
            first = first == RV_OK ? rc : first;
        }
    }
    return first;
}

int64_t rv_pccd_fs::model_retexture_(model_record &model, int64_t index, const rv_pccg_part_texture &next)
{
    // The mirror must equal the controller's state after every call, so a refusal
    // puts back this copy rather than undoing the parts one by one.
    std::vector<rv_pccg_part_texture> before;
    try {
        before = model.textures;
    } catch (const std::bad_alloc &) {
        return RV_ERR_NOMEM;
    }
    set_parts(model.textures, model.part_texture, index, next);
    const int64_t rc = cg_->model_textures_set(model.addr, model.textures);
    if (rc < 0) {
        model.textures = before;
    }
    return rc;
}

void rv_pccd_fs::free_models_()
{
    // Models exist only once cg_ was attached, so a null cg_ means none to free.
    if (cg_ == nullptr) {
        return;
    }
    for (const model_record &record : models_) {
        const int64_t rc = cg_->model_free(record.addr);
        if (rc < 0) {
            RV_LOG_ERR("pccd",
                "model '{}' could not be freed (rv_err {})",
                rv_pdklib::rv_log_escape(record.resname.c_str()),
                rc);
        }
    }
}

} // namespace rv_3dmppc
