#ifndef RV_PDK_CD_RV_CD_H
#define RV_PDK_CD_RV_CD_H

#include <stdint.h>

#include "pdk/cv/rv_vertex.h" // rv_color: scene object tint

// Bytes per name in rv_cd_scene_object, NUL included. The arrays of
// rv_cd_scene_object use the same number; ffi.cdef cannot expand a #define.
// Lua sees it as pdk.CD_NAME_CAPACITY.
#define RV_CD_NAME_CAPACITY 64

// Bytes per component field value, NUL included, and fields per component: the
// fixed sizes of rv_cd_scene_component, so a component is a plain copy like an
// object. A component past either limit does not load its scene (see below).
// Lua sees them as pdk.CD_VALUE_CAPACITY and pdk.CD_COMPONENT_FIELD_CAPACITY.
#define RV_CD_VALUE_CAPACITY           64
#define RV_CD_COMPONENT_FIELD_CAPACITY 8

// Q20.12 fixed point: 12 fraction bits, so 1.0 is 4096. Lua sees them as
// pdk.CD_Q_FRACTION_BITS and pdk.CD_Q_ONE.
#define RV_CD_Q_FRACTION_BITS 12
#define RV_CD_Q_ONE           4096

#ifdef __cplusplus
extern "C" {
#endif

/* RV_CDEF_BEGIN */

typedef struct rv_cd rv_cd;

int64_t rv_cd_asset_open(rv_cd *cd, const char *resname);
int64_t rv_cd_asset_size(rv_cd *cd, int64_t handle);
int64_t rv_cd_asset_read(rv_cd *cd, int64_t handle, void *baddr, int64_t baddr_size);

// What a resource query names. A disc's resources are not only textures -
// the PR review that added this `kind` parameter asked for sound next, and
// model is still the obvious kind after that - so AUDIO is the second
// enumerator and MODEL the third, refused nowhere but the one gate named on
// rv_cd_resource_addr below, the same way TEXTURE alone was until now.
enum rv_cd_resource_kind {
    RV_CD_RESOURCE_TEXTURE = 1,
    RV_CD_RESOURCE_AUDIO = 2,
    RV_CD_RESOURCE_MODEL = 3,
};

/* Plain name: without a typedef, C demands `enum rv_x` at every mention and
   the contract would read as noise. It is also the shape ffi.cdef accepts as
   an ordinary type. */
typedef enum rv_cd_resource_kind rv_cd_resource_kind;

// A disc never acquires or releases a resource - it only ever names one.
// Residency is the drive's business: the first of these five calls to name a
// resource of a given kind makes it resident (for the texture kind: reading
// the asset, decoding its container and uploading it to video RAM; for the
// audio kind: reading the asset's raw PCM bytes - there is no container to
// decode, the game supplies none and none is baked - and uploading them to
// sound RAM; for the model kind: reading the .mppcmod and handing it to the
// geometry controller's model memory, making its textures resident with it),
// and every later call for the same name and kind reuses that upload. The
// drive keeps a resource resident until the disc that named it is unloaded,
// at which point the drive frees everything it made resident - the disc never
// frees anything itself.
//
// Not every one of these five means something for every kind:
//   * rv_cd_resource_addr          - meaningful for TEXTURE, AUDIO and MODEL
//                                    alike: where the resident bytes now live.
//   * rv_cd_resource_size          - AUDIO only: its resident byte length,
//                                    the counterpart of a sample's rv_sample.size.
//                                    A texture's "size" is its width/height
//                                    below, not a byte count a game reads back.
//   * rv_cd_resource_palette_addr,
//     rv_cd_resource_width,
//     rv_cd_resource_height        - TEXTURE only: a sound or a model has no
//                                    palette and no pixel dimensions.
// Asking a kind for a query that does not describe it is exactly as
// malformed as asking with a `kind` this enum has never heard of, and
// answers the same RV_ERR_INVAL rather than a bogus 0 that would read back
// as a real, if unlikely, "no palette" or "0 bytes" - a caller must be able
// to trust that a non-negative answer is real. This is deliberately still
// ONE refusal every resource_* query routes through (see rv_pccd_fs's
// resource_resolve_, the one gate every one of the five reaches), not five
// separate copies of the same check.
// A palette is texture-specific in the other direction too: palette_addr
// answers 0, not an error, for a texture FORMAT that has no palette - that
// is a real fact about a resident texture, not a malformed query - and only
// the kind mismatch above (asking palette_addr of an AUDIO or MODEL name) is
// refused.
// A name that is not on the disc, or a resource that fails to become
// resident (a texture that fails to decode; AUDIO and MODEL have nothing to
// decode), also answers a negative rv_err from any of the five (RV_ERR_NOENT
// for the unknown name, distinct from the RV_ERR_INVAL a bad kind or a
// meaningless query gets).
// Addresses are valid until that resource is reloaded - ask again rather
// than caching across a reload.
int64_t rv_cd_resource_addr(rv_cd *cd, rv_cd_resource_kind kind, const char *resname);
int64_t rv_cd_resource_size(rv_cd *cd, rv_cd_resource_kind kind, const char *resname);
int64_t rv_cd_resource_palette_addr(rv_cd *cd, rv_cd_resource_kind kind, const char *resname);
int64_t rv_cd_resource_width(rv_cd *cd, rv_cd_resource_kind kind, const char *resname);
int64_t rv_cd_resource_height(rv_cd *cd, rv_cd_resource_kind kind, const char *resname);

// Kinds of objects in a scene: the object kinds of the editor's scene.
enum rv_cd_scene_kind {
    RV_CD_SCENE_GROUP = 1,
    RV_CD_SCENE_CAMERA,
    RV_CD_SCENE_MESH,
    RV_CD_SCENE_QUAD,
    RV_CD_SCENE_BILLBOARD,
    RV_CD_SCENE_VOLUME,
};

typedef enum rv_cd_scene_kind rv_cd_scene_kind;

// A scene is a .scene.toml on the disc, resident from its first query and
// freed when the disc unloads. A name longer than RV_CD_NAME_CAPACITY-1 is
// not loaded at all (RV_ERR_INVAL; the reason goes to the console log). A mesh
// is named "x.glb" or "x.gltf" in the scene; the drive serves it as x.mppcmod.
//
// One object as a plain copy. Names are NUL-terminated inside their arrays;
// an empty model or texture means none.
struct rv_cd_scene_object {
    char id[64]; // RV_CD_NAME_CAPACITY, spelled out for ffi.cdef
    char name[64];
    int64_t parent; // index of the parent object, -1 for a root
    rv_cd_scene_kind kind;
    // row-major: element (row, col) is world[row*4+col]; column vectors multiplied
    // from the right, the layout of pdklib's rv_mat4
    float world[16];
    char model[64]; // .mppcmod name on the disc
    char texture[64];
    float uv[4]; // u0, v0, u1, v1 in pixels; quad and billboard only
    struct rv_color tint;
    float tess; // quad/billboard subdivision density, as the editor's scene
};

typedef struct rv_cd_scene_object rv_cd_scene_object;

// Number of objects in the scene, or a negative rv_err (RV_ERR_NOENT for an
// unknown scene).
int64_t rv_cd_scene_object_count(rv_cd *cd, const char *scene);

// Copies object `index` into `out`. RV_ERR_INVAL for an index out of range or
// a null argument, RV_ERR_NOENT for an unknown scene.
int64_t rv_cd_scene_object_read(rv_cd *cd, const char *scene, int64_t index, rv_cd_scene_object *out);

// One field of a component, as text. Key is NUL-terminated; value keeps the text
// rv_scene_render writes: strings in double quotes, arrays as [a, b, c], numbers
// in their shortest fixed form. The quotes tell a string from a number.
struct rv_cd_scene_field {
    char key[64];   // RV_CD_NAME_CAPACITY, spelled out for ffi.cdef
    char value[64]; // RV_CD_VALUE_CAPACITY, spelled out for ffi.cdef
};

typedef struct rv_cd_scene_field rv_cd_scene_field;

// A component of a scene, flattened over its objects in document order; only the
// first field_count fields are in use. Over RV_CD_COMPONENT_FIELD_CAPACITY fields,
// or a key or value past its capacity, the scene does not load (RV_ERR_INVAL;
// the reason goes to the console log).
struct rv_cd_scene_component {
    int64_t object; // index of the object the component belongs to
    char name[64];  // RV_CD_NAME_CAPACITY, spelled out for ffi.cdef
    int64_t field_count;
    rv_cd_scene_field fields[8]; // RV_CD_COMPONENT_FIELD_CAPACITY, spelled out for ffi.cdef
};

typedef struct rv_cd_scene_component rv_cd_scene_component;

// The entity type of an object, NUL-terminated; empty means the base type.
struct rv_cd_scene_entity {
    char type[64]; // RV_CD_NAME_CAPACITY, spelled out for ffi.cdef
};

typedef struct rv_cd_scene_entity rv_cd_scene_entity;

// Number of components in the scene, or a negative rv_err (RV_ERR_NOENT for an
// unknown scene).
int64_t rv_cd_scene_component_count(rv_cd *cd, const char *scene);

// Copies component `index` into `out`. RV_ERR_INVAL for an index out of range or
// a null argument, RV_ERR_NOENT for an unknown scene.
int64_t rv_cd_scene_component_read(rv_cd *cd, const char *scene, int64_t index, rv_cd_scene_component *out);

// Copies the entity type of object `index` into `out`. Errors as for
// rv_cd_scene_object_read.
int64_t rv_cd_scene_object_entity_read(rv_cd *cd, const char *scene, int64_t index, rv_cd_scene_entity *out);

// Local transform in Q20.12 (RV_CD_Q_ONE is 1.0), relative to the parent
// (rv_cd_scene_object.parent; -1 is scene space). position is x, y, z in scene
// units; rotation is pitch (x), yaw (y), roll (z) in degrees, R = Ry * Rx * Rz as
// pdklib rv_scene; scale is dimensionless. Values are Q20.12 numbers of the scene
// document: a version 1.1 file stores them as they are, a version 1.0 file's decimals
// round to the nearest one on load. An integer part outside [-524288, 524287] does not
// load the scene (RV_ERR_INVAL, reason in the console log).
struct rv_cd_scene_local {
    int32_t position[3];
    int32_t rotation[3];
    int32_t scale[3];
};

typedef struct rv_cd_scene_local rv_cd_scene_local;

// Copies the local transform of object `index` into `out`. Errors as for
// rv_cd_scene_object_read.
int64_t rv_cd_scene_object_local_read(rv_cd *cd, const char *scene, int64_t index, rv_cd_scene_local *out);

/* RV_CDEF_END */

// Every name array of the struct must match the name capacity (the cdef carries
// the literal, so only C and C++ can check it).
#ifdef __cplusplus
#define RV_CD_STATIC_ASSERT_ static_assert
#else
#define RV_CD_STATIC_ASSERT_ _Static_assert
#endif
RV_CD_STATIC_ASSERT_(sizeof(((struct rv_cd_scene_object *)0)->id) == RV_CD_NAME_CAPACITY, "scene id capacity");
RV_CD_STATIC_ASSERT_(sizeof(((struct rv_cd_scene_object *)0)->name) == RV_CD_NAME_CAPACITY, "scene name capacity");
RV_CD_STATIC_ASSERT_(sizeof(((struct rv_cd_scene_object *)0)->model) == RV_CD_NAME_CAPACITY, "scene model capacity");
RV_CD_STATIC_ASSERT_(sizeof(((struct rv_cd_scene_object *)0)->texture) == RV_CD_NAME_CAPACITY, "scene texture capacity");
RV_CD_STATIC_ASSERT_(sizeof(((struct rv_cd_scene_field *)0)->key) == RV_CD_NAME_CAPACITY, "scene field key capacity");
RV_CD_STATIC_ASSERT_(sizeof(((struct rv_cd_scene_field *)0)->value) == RV_CD_VALUE_CAPACITY, "scene field value capacity");
RV_CD_STATIC_ASSERT_(sizeof(((struct rv_cd_scene_component *)0)->name) == RV_CD_NAME_CAPACITY, "scene component name capacity");
RV_CD_STATIC_ASSERT_(sizeof(((struct rv_cd_scene_component *)0)->fields) ==
        RV_CD_COMPONENT_FIELD_CAPACITY * sizeof(struct rv_cd_scene_field),
    "scene component field capacity");
RV_CD_STATIC_ASSERT_(sizeof(((struct rv_cd_scene_entity *)0)->type) == RV_CD_NAME_CAPACITY, "scene entity type capacity");
RV_CD_STATIC_ASSERT_(sizeof(rv_cd_scene_local) == 9 * sizeof(int32_t), "scene local transform size");
RV_CD_STATIC_ASSERT_((1 << RV_CD_Q_FRACTION_BITS) == RV_CD_Q_ONE, "Q20.12 one is 1 << fraction bits");

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif // RV_PDK_CD_RV_CD_H
