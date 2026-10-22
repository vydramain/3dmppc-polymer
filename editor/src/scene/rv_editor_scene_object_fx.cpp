// Q20.12 accessors of an object's transform and tess: the raw values of their fields, no conversion.
// The double accessors in rv_editor_scene_object.cpp read through these.

#include "scene/rv_editor_scene.hpp"

#include <array>
#include <string_view>

#include "pdklib/rv_math/rv_fx.hpp"
#include "scene/rv_editor_scene_fields.hpp"
#include "scene/rv_editor_scene_object_detail.hpp"

namespace rv_editor
{

namespace
{

// Tess of a sprite whose tess field is absent, as pdkkit gives it: 2 in Q20.12.
constexpr rv_pdklib::fx default_tess_fx = 2 * rv_pdklib::fx_one;
// Scale of an object whose scale field is absent: 1 in Q20.12 on each axis.
constexpr std::array<rv_pdklib::fx, vec3_size> default_scale_fx = { rv_pdklib::fx_one, rv_pdklib::fx_one, rv_pdklib::fx_one };

// The raw Q20.12 values of a field of component c; def when c or the field is absent. Load refuses a field that
// does not read, so an unreadable field cannot reach here.
template <size_t count>
std::array<rv_pdklib::fx, count>
read_raw(const rv_editor_scene_component *c, std::string_view key, const std::array<rv_pdklib::fx, count> &def)
{
    std::array<rv_pdklib::fx, count> raw{};
    std::string error;
    if (c == nullptr || rv_editor_fields_read_fx<count>(*c, key, raw, error) != RV_OK) {
        return def;
    }
    return raw;
}

// The accessors speak fx_vec3; the field helpers take arrays.
rv_pdklib::fx_vec3 vec3_of(const std::array<rv_pdklib::fx, vec3_size> &a)
{
    return { a[0], a[1], a[2] };
}

} // namespace

rv_pdklib::fx_vec3 rv_editor_object_get_position_fx(const rv_editor_scene_object &o)
{
    return vec3_of(read_raw<vec3_size>(rv_editor_scene_component_find(o, names().transform), names().position, { 0, 0, 0 }));
}

void rv_editor_object_set_position_fx(rv_editor_scene_object &o, const rv_pdklib::fx_vec3 &value)
{
    rv_editor_fields_write_fx(rv_editor_fields_component_ensure(o, names().transform), names().position, array_of(value));
}

rv_pdklib::fx_vec3 rv_editor_object_get_rotation_fx(const rv_editor_scene_object &o)
{
    return vec3_of(read_raw<vec3_size>(rv_editor_scene_component_find(o, names().transform), names().rotation, { 0, 0, 0 }));
}

void rv_editor_object_set_rotation_fx(rv_editor_scene_object &o, const rv_pdklib::fx_vec3 &value)
{
    rv_editor_fields_write_fx(rv_editor_fields_component_ensure(o, names().transform), names().rotation, array_of(value));
}

rv_pdklib::fx_vec3 rv_editor_object_get_scale_fx(const rv_editor_scene_object &o)
{
    return vec3_of(read_raw<vec3_size>(rv_editor_scene_component_find(o, names().transform), names().scale, default_scale_fx));
}

void rv_editor_object_set_scale_fx(rv_editor_scene_object &o, const rv_pdklib::fx_vec3 &value)
{
    rv_editor_fields_write_fx(rv_editor_fields_component_ensure(o, names().transform), names().scale, array_of(value));
}

rv_pdklib::fx rv_editor_object_get_tess_fx(const rv_editor_scene_object &o)
{
    return read_raw<1>(sprite_of(o), names().legacy_tess, { default_tess_fx })[0];
}

void rv_editor_object_set_tess_fx(rv_editor_scene_object &o, rv_pdklib::fx value)
{
    rv_editor_scene_component *c = sprite_of(o);
    if (c == nullptr) {
        refuse(o, names().legacy_tess, reason_no_sprite);
        return;
    }
    if (value <= 0) {
        refuse(o, names().legacy_tess, reason_tess_positive);
        return;
    }
    rv_editor_fields_write_fx(*c, names().legacy_tess, std::array<rv_pdklib::fx, 1>{ value });
}

} // namespace rv_editor
