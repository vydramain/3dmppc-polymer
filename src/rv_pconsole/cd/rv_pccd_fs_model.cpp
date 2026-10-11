// rv_pccd_fs MODEL residency: a .mppcmod read from the disc, its textures made
// resident through the TEXTURE path, and the whole model uploaded to the
// geometry controller. Kept out of rv_pccd_fs.cpp, which is at its size limit.
#include <new>
#include <utility>
#include <string>
#include <vector>

#include "pdk/rv_err.h"
#include "pdklib/rv_logs/rv_logs.hpp"
#include "pdklib/rv_models/rv_mppcmod.hpp"
#include "rv_pconsole/cd/rv_pccd_fs.hpp"
#include "rv_pconsole/cg/rv_pccg.hpp"

namespace rv_3dmppc
{

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

    rv_pdklib::rv_mppcmod model;
    std::string error;
    if (rv_pdklib::rv_mppcmod_parse(bytes, model, error) != RV_OK) {
        RV_LOG_WARN("pccd", "model '{}' refused: it {}", rv_pdklib::rv_log_escape(resname), error);
        return RV_ERR_INVAL;
    }

    // One entry per part, in part order, as model_upload() requires; a part
    // without a texture keeps addr_texture 0.
    std::vector<rv_pccg_part_texture> textures(model.parts.size(), rv_pccg_part_texture{ 0, 0, 0, 0 });
    std::vector<std::pair<int64_t, int64_t>> texture_addrs(model.parts.size(), { 0, 0 });
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
        texture_addrs[p] = { texture->tex_addr, texture->pal_addr };
    }

    const int64_t addr = cg_->model_upload(model, textures);
    if (addr < 0) {
        RV_LOG_WARN("pccd", "model '{}' not uploaded (rv_err {})", rv_pdklib::rv_log_escape(resname), addr);
        return addr;
    }

    model_record record;
    record.resname = resname;
    record.addr = addr;
    record.texture_addrs = std::move(texture_addrs);

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

const rv_pccd_fs::model_record *rv_pccd_fs::model_using_texture_(int64_t tex_addr, int64_t pal_addr) const
{
    for (const model_record &model : models_) {
        for (const auto &[tex, pal] : model.texture_addrs) {
            if ((tex != 0 && tex == tex_addr) || (pal != 0 && pal == pal_addr)) {
                return &model;
            }
        }
    }
    return nullptr;
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
