// Integer world matrices of scene objects: the chain of rv_editor_scene_world, in Q20.12 and 4.12.

#include "scene/rv_editor_scene_edit.hpp"

#include "pdk/rv_err.h"
#include "pdklib/rv_gte/rv_gte_matrix.hpp"
#include "pdklib/rv_gte/rv_gte_trs.hpp"

namespace rv_editor
{

int rv_editor_scene_local_gte(const rv_editor_scene_object &o, rv_pdklib::rv_gte_matrix &out)
{
    const rv_pdklib::fx_vec3 position = rv_editor_object_get_position_fx(o);
    const rv_pdklib::fx_vec3 rotation = rv_editor_object_get_rotation_fx(o);
    const rv_pdklib::fx_vec3 scale = rv_editor_object_get_scale_fx(o);
    rv_pdklib::rv_gte_matrix local{};
    if (rv_pdklib::rv_gte_trs(position, rotation, scale, local) != RV_OK) {
        return RV_ERR_INVAL;
    }
    out = local;
    return RV_OK;
}

int rv_editor_scene_world_gte(const rv_editor_scene &scene, int index, rv_pdklib::rv_gte_matrix &out)
{
    const bool outside = index < 0 || static_cast<size_t>(index) >= scene.objects.size();
    if (outside) {
        return RV_ERR_INVAL;
    }
    const rv_editor_scene_object &first = scene.objects[static_cast<size_t>(index)];
    rv_pdklib::rv_gte_matrix m{};
    if (rv_editor_scene_local_gte(first, m) != RV_OK) {
        return RV_ERR_INVAL;
    }
    int p = rv_editor_scene_find(scene, first.parent);
    for (size_t steps = 0; p >= 0 && steps < scene.objects.size(); ++steps) {
        const rv_editor_scene_object &up = scene.objects[static_cast<size_t>(p)];
        rv_pdklib::rv_gte_matrix up_local{};
        if (rv_editor_scene_local_gte(up, up_local) != RV_OK) {
            return RV_ERR_INVAL;
        }
        rv_pdklib::rv_gte_matrix next{};
        if (rv_pdklib::rv_gte_compose(up_local, m, next) != RV_OK) {
            return RV_ERR_INVAL;
        }
        m = next;
        p = rv_editor_scene_find(scene, up.parent);
    }
    // An acyclic chain has fewer ancestors than objects, so a parent left after the steps is a cycle.
    if (p >= 0) {
        return RV_ERR_INVAL;
    }
    out = m;
    return RV_OK;
}

} // namespace rv_editor
