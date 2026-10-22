#include "rv_pconsole/cg/rv_pccg_sw.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <string>
#include <vector>

#include "pdk/rv_err.h"
#include "pdklib/rv_logs/rv_logs.hpp"
#include "pdklib/rv_models/rv_mppcmod_int.hpp"
#include "rv_pconsole/rv_pcbudget.hpp"

namespace rv_3dmppc
{

namespace
{

// Model addresses start here, not at 0: 0 is what a part without a texture carries,
// and a model must never be mistaken for it. Positive, so a disc never reads it as an error.
constexpr int64_t RV_PCCG_SW_FIRST_ADDR = 16;

// The bytes a model costs against model_memory_size: its .mppcmod size, field by field
// as the format lays it out. The budget is in the burner's units, so a model that fits
// a disc fits the console; the decoded copy kept in host memory is not what is charged.
int64_t model_cost(const rv_pdklib::rv_mppcmod_int_model &model)
{
    int64_t bytes = rv_pdklib::rv_mppcmod_int_header_size;
    for (const rv_pdklib::rv_mppcmod_part &part : model.parts) {
        bytes += rv_pdklib::RV_MPPCMOD_PART_FIXED_BYTES + static_cast<int64_t>(part.texture.size());
    }
    bytes += static_cast<int64_t>(model.vertices.size()) * rv_pdklib::RV_MPPCMOD_INT_VERT_BYTES;
    bytes += static_cast<int64_t>(model.triangles.size()) * rv_pdklib::rv_mppcmod_triangle_bytes;
    return bytes;
}

// The smallest model that is accepted: the header, one vertex and one triangle. A model
// with no part is valid, so no part is counted. Every accepted model costs at least this,
// so the record array sized from it is never outgrown.
constexpr int64_t RV_PCCG_SW_MIN_MODEL_BYTES =
    rv_pdklib::rv_mppcmod_int_header_size + rv_pdklib::RV_MPPCMOD_INT_VERT_BYTES + rv_pdklib::rv_mppcmod_triangle_bytes;

// The most models model_memory_size can hold, each costing at least the minimum.
int64_t max_models(int64_t model_memory_size)
{
    return model_memory_size / RV_PCCG_SW_MIN_MODEL_BYTES;
}

int64_t ceil_div(int64_t a, int64_t b)
{
    return (a + b - 1) / b;
}

// The most host bytes any one record kind takes per .mppcmod byte, rounded up to a whole
// number so the bound is an integer. Counted: the struct sizes and the heap bytes of every
// name, len+1 each (an over-count for a name inside the inline buffer). Not counted:
// allocator overhead.
// A part is C = sizeof(rv_mppcmod_part) + sizeof(rv_pccg_part_texture), plus the heap bytes
// of its name, len+1, against the 12 fixed bytes. The +1 is taken at zero length, which
// bounds every length since (C+1+len)/(12+len) never exceeds (C+1)/12 for C >= 11.
// Vertices and triangles are their struct sizes against their record size.
int64_t host_bytes_per_file_byte()
{
    const int64_t part_bytes = static_cast<int64_t>(sizeof(rv_pdklib::rv_mppcmod_part) + sizeof(rv_pccg_part_texture) + 1);
    const int64_t part = ceil_div(part_bytes, rv_pdklib::RV_MPPCMOD_PART_FIXED_BYTES);
    const int64_t vertex =
        ceil_div(static_cast<int64_t>(sizeof(rv_pdklib::rv_mppcmod_int_vertex)), rv_pdklib::RV_MPPCMOD_INT_VERT_BYTES);
    const int64_t triangle =
        ceil_div(static_cast<int64_t>(sizeof(rv_pdklib::rv_mppcmod_triangle)), rv_pdklib::rv_mppcmod_triangle_bytes);
    return std::max({ part, vertex, triangle });
}

// A NaN or an inf in eye, target or up poisons the projection as surely as a bad fov or
// plane, so all twelve floats are refused together.
bool camera_finite(const rv_cg_camera &camera)
{
    const std::array<float, 12> values = { camera.eye.x,
        camera.eye.y,
        camera.eye.z,
        camera.target.x,
        camera.target.y,
        camera.target.z,
        camera.up.x,
        camera.up.y,
        camera.up.z,
        camera.fov_y,
        camera.near_plane,
        camera.far_plane };
    return std::all_of(values.begin(), values.end(), [](float value) {
        return std::isfinite(value);
    });
}

// One texture entry per part, in part order: the rule model_upload and model_textures_set share.
bool textures_per_part(const rv_pdklib::rv_mppcmod_int_model &model, std::span<const rv_pccg_part_texture> textures)
{
    return textures.size() == model.parts.size();
}

// rv_cg_camera holds the field of view in radians; rv_gte_camera_make takes degrees.
constexpr double RV_PCCG_SW_DEGREES_PER_RADIAN = 180.0 / std::numbers::pi;

// The integer camera of the frame. transitional until E11e-2: rv_cg.h is float, so the conversion
// to Q20.12 happens here, once per frame. Error: a value leaves Q20.12, the triangle depth key span is
// above gte_key_span_max, or rv_gte_camera_make refuses the camera; out is untouched.
int gte_camera_make(const rv_cg_camera &camera, int32_t screen_height, rv_pdklib::rv_gte_camera &out)
{
    rv_pdklib::fx_vec3 eye = {};
    if (rv_pccg_sw_fx_vec3(camera.eye, eye) != RV_OK) {
        RV_LOG_WARN("pccg", "camera refused: eye leaves Q20.12");
        return RV_ERR_INVAL;
    }
    rv_pdklib::fx_vec3 target = {};
    if (rv_pccg_sw_fx_vec3(camera.target, target) != RV_OK) {
        RV_LOG_WARN("pccg", "camera refused: target leaves Q20.12");
        return RV_ERR_INVAL;
    }
    rv_pdklib::fx_vec3 up = {};
    if (rv_pccg_sw_fx_vec3(camera.up, up) != RV_OK) {
        RV_LOG_WARN("pccg", "camera refused: up leaves Q20.12");
        return RV_ERR_INVAL;
    }
    rv_pdklib::fx fov = 0;
    if (rv_pdklib::fx_from_double(static_cast<double>(camera.fov_y) * RV_PCCG_SW_DEGREES_PER_RADIAN, fov) != RV_OK) {
        RV_LOG_WARN("pccg", "camera refused: fov leaves Q20.12");
        return RV_ERR_INVAL;
    }
    rv_pdklib::fx near_plane = 0;
    if (rv_pdklib::fx_from_double(camera.near_plane, near_plane) != RV_OK) {
        RV_LOG_WARN("pccg", "camera refused: near plane leaves Q20.12");
        return RV_ERR_INVAL;
    }
    rv_pdklib::fx far_plane = 0;
    if (rv_pdklib::fx_from_double(camera.far_plane, far_plane) != RV_OK) {
        RV_LOG_WARN("pccg", "camera refused: far plane leaves Q20.12");
        return RV_ERR_INVAL;
    }
    // rv_gte_depth_key takes count * (far minus near) and refuses above gte_key_span_max. A triangle takes
    // RV_PCCG_SW_TRI_CORNERS points, so a larger span would refuse every triangle of the frame.
    const int64_t span = static_cast<int64_t>(far_plane) - near_plane;
    const int64_t triangle_span = span * RV_PCCG_SW_TRI_CORNERS;
    if (triangle_span > rv_pdklib::gte_key_span_max) {
        RV_LOG_WARN("pccg",
            "camera refused: three times far minus near is {} in Q20.12, above the depth key span {}",
            triangle_span,
            rv_pdklib::gte_key_span_max);
        return RV_ERR_INVAL;
    }
    if (rv_pdklib::rv_gte_camera_make(eye, target, up, fov, screen_height, near_plane, far_plane, out) != RV_OK) {
        RV_LOG_WARN("pccg",
            "camera refused: rv_gte_camera_make has no valid camera (fov outside 10 to 180 degrees, planes, "
            "screen height, eye on target, up along the aim, or h below 1 px)");
        return RV_ERR_INVAL;
    }
    return RV_OK;
}

} // namespace

int rv_pccg_sw::evaluate(const rv_pdklib::rv_manifest_budget &budget, int64_t &bytes, std::string &error)
{
    int64_t total = 0;

    // Decoded models: at most host_bytes_per_file_byte() host bytes for each of the
    // model_memory_size bytes of .mppcmod the memory is charged with.
    int64_t models_bytes = 0;
    const int models_mul_rc = rv_pcbudget_mul("budget.pccg.model_memory_size",
        budget.pccg.model_memory_size,
        host_bytes_per_file_byte(),
        models_bytes,
        error);
    if (models_mul_rc != RV_OK) {
        return models_mul_rc;
    }

    const int models_add_rc = rv_pcbudget_add(total, "budget.pccg.model_memory_size", models_bytes, error);
    if (models_add_rc != RV_OK) {
        return models_add_rc;
    }

    // One model_record per model that the smallest .mppcmod (its header alone) allows.
    int64_t records_bytes = 0;
    const int records_mul_rc = rv_pcbudget_mul("budget.pccg.model_memory_size",
        max_models(budget.pccg.model_memory_size),
        static_cast<int64_t>(sizeof(model_record)),
        records_bytes,
        error);
    if (records_mul_rc != RV_OK) {
        return records_mul_rc;
    }

    const int records_add_rc = rv_pcbudget_add(total, "budget.pccg.model_memory_size", records_bytes, error);
    if (records_add_rc != RV_OK) {
        return records_add_rc;
    }

    bytes = total;
    return RV_OK;
}

rv_pccg_sw::rv_pccg_sw(const rv_pccg_conf &conf, rv_pccv &cv)
    : conf_(conf)
    , cv_(cv)
    , next_addr_(RV_PCCG_SW_FIRST_ADDR)
{
    // Reserved at construction, as ca and cv reserve their memory: the record array
    // never reallocates, because no accepted model costs less than the minimum.
    models_.reserve(static_cast<size_t>(max_models(conf_.model_memory_size)));

    RV_LOG_INFO("pccg", "{} byte(s) of model memory, {} triangles per frame", conf_.model_memory_size, conf_.triangle_capacity);
}

// --- budget ------------------------------------------------------------------

int64_t rv_pccg_sw::model_memory_size()
{
    return conf_.model_memory_size;
}

int64_t rv_pccg_sw::triangle_capacity()
{
    return conf_.triangle_capacity;
}

// --- the frame ---------------------------------------------------------------

int64_t rv_pccg_sw::frame_camera(const rv_cg_camera *camera)
{
    const bool absent = camera == nullptr;
    if (absent) {
        return RV_ERR_INVAL;
    }
    const bool not_finite = !camera_finite(*camera);
    if (not_finite) {
        return RV_ERR_INVAL;
    }
    // Written as "not greater than zero" so a NaN is refused as well.
    const bool bad_fov = !(camera->fov_y > 0.0f);
    if (bad_fov) {
        return RV_ERR_INVAL;
    }
    const bool bad_planes = !(camera->near_plane > 0.0f && camera->near_plane < camera->far_plane);
    if (bad_planes) {
        return RV_ERR_INVAL;
    }
    int32_t height = 0;
    if (rv_pccg_sw_screen_side(cv_.screen_height(), height) != RV_OK) {
        RV_LOG_WARN("pccg", "camera refused: the screen height is outside (0, INT32_MAX]");
        return RV_ERR_INVAL;
    }
    rv_pdklib::rv_gte_camera gte_camera = {};
    if (gte_camera_make(*camera, height, gte_camera) != RV_OK) {
        return RV_ERR_INVAL;
    }
    gte_camera_ = gte_camera;
    camera_set_ = true;
    return RV_OK;
}

// --- console-side ------------------------------------------------------------

void rv_pccg_sw::frame_reset()
{
    camera_set_ = false;
    triangles_ = 0;
}

int64_t rv_pccg_sw::model_upload(const rv_pdklib::rv_mppcmod_int_model &model, std::span<const rv_pccg_part_texture> textures)
{
    const bool texture_per_part = textures_per_part(model, textures);
    if (!texture_per_part) {
        return RV_ERR_INVAL;
    }
    const bool no_triangles = model.triangles.empty();
    if (no_triangles) {
        RV_LOG_WARN("pccg", "model refused: it has no triangles, so there is nothing to draw");
        return RV_ERR_INVAL;
    }
    std::string error;
    const int format_rc = rv_pdklib::rv_mppcmod_int_validate(model, error);
    if (format_rc != RV_OK) {
        RV_LOG_WARN("pccg", "model refused: {}", error);
        return RV_ERR_INVAL;
    }
    const int64_t bytes = model_cost(model);
    // Compared as the remainder, so the sum can never overflow.
    const bool fits = bytes <= conf_.model_memory_size - used_bytes_;
    if (!fits) {
        return RV_ERR_NOMEM;
    }
    const int64_t addr = next_addr_++;
    models_.push_back({ addr, bytes, model, std::vector<rv_pccg_part_texture>(textures.begin(), textures.end()) });
    used_bytes_ += bytes;
    return addr;
}

int64_t rv_pccg_sw::model_textures_set(int64_t addr, std::span<const rv_pccg_part_texture> textures)
{
    const auto it = find(addr);
    const bool unknown = it == models_.end();
    if (unknown) {
        RV_LOG_WARN("pccg", "textures refused: no model at address {}", addr);
        return RV_ERR_INVAL;
    }
    const bool texture_per_part = textures_per_part(it->int_model, textures);
    if (!texture_per_part) {
        RV_LOG_WARN("pccg", "textures refused: {} texture(s) for {} part(s)", textures.size(), it->int_model.parts.size());
        return RV_ERR_INVAL;
    }
    // Only the texture entries change: the bytes charged and the record's address stay.
    it->textures.assign(textures.begin(), textures.end());
    return RV_OK;
}

int64_t rv_pccg_sw::model_free(int64_t addr)
{
    const auto it = find(addr);
    const bool unknown = it == models_.end();
    if (unknown) {
        return RV_ERR_INVAL;
    }
    used_bytes_ -= it->bytes;
    models_.erase(it);
    return RV_OK;
}

std::vector<rv_pccg_sw::model_record>::iterator rv_pccg_sw::find(int64_t addr)
{
    return std::find_if(models_.begin(), models_.end(), [addr](const model_record &record) {
        return record.addr == addr;
    });
}

} // namespace rv_3dmppc
