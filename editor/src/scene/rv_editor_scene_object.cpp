// Scene object data: the component and field names pdkkit's scene uses, the kind and data tables, and the
// accessors that read and write an object's data through its components.

#include "scene/rv_editor_scene.hpp"

#include <algorithm>
#include <array>
#include <initializer_list>
#include <string>
#include <string_view>
#include <vector>

#include "pdk/rv_err.h"
#include "pdklib/rv_logs/rv_logs.hpp"
#include "pdklib/rv_math/rv_fx.hpp"
#include "pdkkit/scene/rv_scene.hpp"
#include "pdkkit/scene/rv_scene_migrate.hpp"
#include "scene/rv_editor_scene_fields.hpp"
#include "scene/rv_editor_scene_object_detail.hpp"

namespace rv_editor
{

namespace
{

// Reasons the refusals and warnings give; each names what was refused, after the object and field.
constexpr std::string_view reason_outside_q20_12 = "is outside Q20.12";
// The words of fields_err_channel in rv_editor_scene_fields.hpp, without its leading space.
constexpr std::string_view reason_tint_outside = "is outside 0..255";
constexpr std::string_view reason_model_source_missing = "model source is missing";

// A kind marker: a component whose presence gives the object its kind. A mesh's kind is its model.
struct kind_row {
    std::string component;
    std::string_view kind;
};

const std::vector<kind_row> &kind_rows()
{
    static const std::vector<kind_row> rows = {
        { names().camera, kind_camera },
        { names().volume, kind_volume },
        { names().model, kind_mesh },
        { names().legacy_quad, kind_quad },
        { names().legacy_billboard, kind_billboard },
    };
    return rows;
}

using check_fn = int (*)(const rv_editor_scene_component &c, std::string &error);

// A data component and the check of its fields. The kind markers have no fields.
struct data_row {
    std::string component;
    check_fn check;
};

// Model numbers of Q20.12 values; exact, the file's units are 1/4096.
template <size_t count>
std::array<double, count> units_of(const std::array<rv_pdklib::fx, count> &raw)
{
    std::array<double, count> out{};
    for (size_t i = 0; i < count; ++i) {
        out[i] = static_cast<double>(raw[i]) / rv_pdklib::fx_one;
    }
    return out;
}

// Q20.12 of model numbers. RV_ERR_INVAL, with a refusal, when one lies outside Q20.12 (rv_pdklib::fx_from_double refuses it).
template <size_t count>
int to_fx(const rv_editor_scene_object &o,
    std::string_view field,
    const std::array<double, count> &value,
    std::array<rv_pdklib::fx, count> &raw)
{
    for (size_t i = 0; i < count; ++i) {
        if (rv_pdklib::fx_from_double(value[i], raw[i]) != RV_OK) {
            refuse(o, field, reason_outside_q20_12);
            return RV_ERR_INVAL;
        }
    }
    return RV_OK;
}

// The text of field key in c; "" when c or the field is absent.
std::string read_text(const rv_editor_scene_component *c, std::string_view key)
{
    std::string text;
    std::string error;
    if (c == nullptr || rv_editor_fields_read_string(*c, key, text, error) != RV_OK) {
        return std::string();
    }
    return text;
}

// An absent field reads as its default; a field that does not read refuses the file.
bool read_ok(int code)
{
    return code == RV_OK || code == RV_ERR_NOENT;
}

// The component that holds the tint (the model or a sprite), or nullptr.
template <typename Object>
auto tint_of(Object &o) -> decltype(&o.components.front())
{
    return find_first(o, { names().model, names().legacy_quad, names().legacy_billboard });
}

// The tint's field in c: a model's tint or a sprite's.
const std::string &tint_key(const rv_editor_scene_component &c)
{
    return c.name == names().model ? names().model_tint : names().legacy_tint;
}

// Checks of the data components: each reads its fields; an absent field is fine, one that does not read refuses.
int check_name(const rv_editor_scene_component &c, std::string &error)
{
    std::string text;
    return read_ok(rv_editor_fields_read_string(c, names().name_text, text, error)) ? RV_OK : RV_ERR_INVAL;
}

int check_transform(const rv_editor_scene_component &c, std::string &error)
{
    const scene_names &n = names();
    std::array<rv_pdklib::fx, vec3_size> v{};
    if (!read_ok(rv_editor_fields_read_fx<vec3_size>(c, n.position, v, error)) ||
        !read_ok(rv_editor_fields_read_fx<vec3_size>(c, n.rotation, v, error)) ||
        !read_ok(rv_editor_fields_read_fx<vec3_size>(c, n.scale, v, error))) {
        return RV_ERR_INVAL;
    }
    return RV_OK;
}

// A model needs its source: pdkkit's model refuses a missing or empty one, and the game would not load the scene.
int check_model(const rv_editor_scene_component &c, std::string &error)
{
    std::string text;
    rv_editor_tint tint{};
    const int source = rv_editor_fields_read_string(c, names().model_source, text, error);
    if (source == RV_ERR_NOENT || (source == RV_OK && text.empty())) {
        error = std::string(reason_model_source_missing);
        return RV_ERR_INVAL;
    }
    if (source != RV_OK || !read_ok(rv_editor_fields_read_tint(c, names().model_tint, tint, error))) {
        return RV_ERR_INVAL;
    }
    return RV_OK;
}

// Tess must be above zero after rounding, as the save has always checked it; absent, it is the default.
int check_tess(const rv_editor_scene_component &c, std::string &error)
{
    std::array<rv_pdklib::fx, 1> tess{};
    const int code = rv_editor_fields_read_fx<1>(c, names().legacy_tess, tess, error);
    if (code == RV_ERR_NOENT) {
        return RV_OK;
    }
    if (code != RV_OK) {
        return RV_ERR_INVAL;
    }
    if (tess[0] <= 0) {
        error = "'tess' " + std::string(reason_tess_positive);
        return RV_ERR_INVAL;
    }
    return RV_OK;
}

int check_sprite(const rv_editor_scene_component &c, std::string &error)
{
    const scene_names &n = names();
    std::string text;
    std::array<rv_pdklib::fx, uv_size> uv{};
    rv_editor_tint tint{};
    if (!read_ok(rv_editor_fields_read_string(c, n.legacy_texture, text, error)) ||
        !read_ok(rv_editor_fields_read_fx<uv_size>(c, n.legacy_uv, uv, error)) ||
        !read_ok(rv_editor_fields_read_tint(c, n.legacy_tint, tint, error))) {
        return RV_ERR_INVAL;
    }
    return check_tess(c, error);
}

const std::vector<data_row> &data_rows()
{
    static const std::vector<data_row> rows = {
        { names().name, check_name },
        { names().transform, check_transform },
        { names().model, check_model },
        { names().legacy_quad, check_sprite },
        { names().legacy_billboard, check_sprite },
    };
    return rows;
}

} // namespace

int rv_editor_scene_object_check(const rv_editor_scene_object &o, std::string &error)
{
    for (const rv_editor_scene_component &c : o.components) {
        for (const data_row &row : data_rows()) {
            if (row.component == c.name && row.check(c, error) != RV_OK) {
                error = "object '" + o.id + "': " + error;
                return RV_ERR_INVAL;
            }
        }
    }
    return RV_OK;
}

bool rv_editor_scene_component_is_kind(std::string_view name)
{
    for (const kind_row &row : kind_rows()) {
        if (row.component == name) {
            return true;
        }
    }
    return false;
}

bool rv_editor_scene_component_is_data(std::string_view name)
{
    for (const data_row &row : data_rows()) {
        if (row.component == name) {
            return true;
        }
    }
    return rv_editor_scene_component_is_kind(name);
}

std::string rv_editor_object_get_name(const rv_editor_scene_object &o)
{
    return read_text(rv_editor_scene_component_find(o, names().name), names().name_text);
}

void rv_editor_object_set_name(rv_editor_scene_object &o, const std::string &value)
{
    rv_editor_fields_write_string(rv_editor_fields_component_ensure(o, names().name), names().name_text, value);
}

std::string rv_editor_object_get_kind(const rv_editor_scene_object &o)
{
    for (const rv_editor_scene_component &c : o.components) {
        for (const kind_row &row : kind_rows()) {
            if (row.component == c.name) {
                return std::string(row.kind);
            }
        }
    }
    return std::string(kind_group);
}

// Creation only: adds the kind's marker to an object without one; a marker is never removed. A group has no row, so
// asking for one adds nothing. The kind after the call is logged when it is not the one asked.
void rv_editor_object_set_kind(rv_editor_scene_object &o, const std::string &value)
{
    for (const kind_row &row : kind_rows()) {
        if (row.kind == value) {
            rv_editor_fields_component_ensure(o, row.component);
            break;
        }
    }
    const std::string kind = rv_editor_object_get_kind(o);
    if (kind != value) {
        RV_LOG_WARN(scene_log_tag, "object '{}': kind '{}' asked, the object stays '{}'", o.id, value, kind);
    }
}

rv_editor_vec3 rv_editor_object_get_position(const rv_editor_scene_object &o)
{
    return units_of(array_of(rv_editor_object_get_position_fx(o)));
}

void rv_editor_object_set_position(rv_editor_scene_object &o, const rv_editor_vec3 &value)
{
    std::array<rv_pdklib::fx, vec3_size> raw{};
    if (to_fx(o, names().position, value, raw) != RV_OK) {
        return;
    }
    rv_editor_fields_write_fx(rv_editor_fields_component_ensure(o, names().transform), names().position, raw);
}

rv_editor_vec3 rv_editor_object_get_rotation(const rv_editor_scene_object &o)
{
    return units_of(array_of(rv_editor_object_get_rotation_fx(o)));
}

void rv_editor_object_set_rotation(rv_editor_scene_object &o, const rv_editor_vec3 &value)
{
    std::array<rv_pdklib::fx, vec3_size> raw{};
    if (to_fx(o, names().rotation, value, raw) != RV_OK) {
        return;
    }
    rv_editor_fields_write_fx(rv_editor_fields_component_ensure(o, names().transform), names().rotation, raw);
}

rv_editor_vec3 rv_editor_object_get_scale(const rv_editor_scene_object &o)
{
    return units_of(array_of(rv_editor_object_get_scale_fx(o)));
}

void rv_editor_object_set_scale(rv_editor_scene_object &o, const rv_editor_vec3 &value)
{
    std::array<rv_pdklib::fx, vec3_size> raw{};
    if (to_fx(o, names().scale, value, raw) != RV_OK) {
        return;
    }
    rv_editor_fields_write_fx(rv_editor_fields_component_ensure(o, names().transform), names().scale, raw);
}

std::string rv_editor_object_get_mesh(const rv_editor_scene_object &o)
{
    return read_text(rv_editor_scene_component_find(o, names().model), names().model_source);
}

void rv_editor_object_set_mesh(rv_editor_scene_object &o, const std::string &value)
{
    // The game refuses a model without a source, so an empty source removes the model.
    if (value.empty()) {
        std::erase_if(o.components, [](const rv_editor_scene_component &c) {
            return c.name == names().model;
        });
        return;
    }
    rv_editor_fields_write_string(rv_editor_fields_component_ensure(o, names().model), names().model_source, value);
}

std::string rv_editor_object_get_texture(const rv_editor_scene_object &o)
{
    return read_text(sprite_of(o), names().legacy_texture);
}

void rv_editor_object_set_texture(rv_editor_scene_object &o, const std::string &value)
{
    rv_editor_scene_component *c = sprite_of(o);
    if (c == nullptr) {
        refuse(o, names().legacy_texture, reason_no_sprite);
        return;
    }
    rv_editor_fields_write_string(*c, names().legacy_texture, value);
}

rv_editor_tint rv_editor_object_get_tint(const rv_editor_scene_object &o)
{
    const rv_editor_scene_component *c = tint_of(o);
    rv_editor_tint tint{};
    std::string error;
    if (c == nullptr || rv_editor_fields_read_tint(*c, tint_key(*c), tint, error) != RV_OK) {
        return default_object_tint;
    }
    return tint;
}

void rv_editor_object_set_tint(rv_editor_scene_object &o, const rv_editor_tint &value)
{
    rv_editor_scene_component *c = tint_of(o);
    if (c == nullptr) {
        refuse(o, names().legacy_tint, "has no mesh, quad or billboard component");
        return;
    }
    for (const int channel : value) {
        if (channel < 0 || channel > fields_channel_max) {
            refuse(o, names().legacy_tint, reason_tint_outside);
            return;
        }
    }
    rv_editor_fields_write_tint(*c, tint_key(*c), value);
}

double rv_editor_object_get_tess(const rv_editor_scene_object &o)
{
    return static_cast<double>(rv_editor_object_get_tess_fx(o)) / rv_pdklib::fx_one;
}

void rv_editor_object_set_tess(rv_editor_scene_object &o, double value)
{
    std::array<rv_pdklib::fx, 1> raw{};
    if (to_fx<1>(o, names().legacy_tess, { value }, raw) != RV_OK) {
        return;
    }
    rv_editor_object_set_tess_fx(o, raw[0]);
}

} // namespace rv_editor
