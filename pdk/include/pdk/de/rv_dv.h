#ifndef RV_PDK_DE_RV_DV_H
#define RV_PDK_DE_RV_DV_H

#include <stdint.h>

#include "pdk/de/rv_de.h"

#define RV_MPPC_SECTION_NAME_DEF ".note.rv_mppc_ver"

#define RV_MPPC_NOTE_OWNER_DEF "RV_MPPC_VER"
#define RV_MPPC_NOTE_MAGIC_DEF "RV_MPPC"

#define RV_MPPC_NOTE_OWNER RV_MPPC_NOTE_OWNER_DEF

// The version numbers are #define and not enum. In C++ an enumeration carries a
// type of its own, std::formatter is not specialized for it, and the very first
// attempt to print the version (rv_pcloader does exactly that when it reports an
// incompatibility) turns into a compile error inside a template. A macro is
// merely an int.
#define RV_MPPC_VER_MAJOR 0
#define RV_MPPC_VER_MINOR 0

#define RV_MPPC_NOTE_TYPE 1

#define RV_MPPC_STR_DEF_(x) #x
#define RV_MPPC_STR_DEF(x)  RV_MPPC_STR_DEF_(x)

#define RV_MPPC_DISC_ENTRY_CREATE_DEF  rv_mppc_disc_entry_create_fn
#define RV_MPPC_DISC_ENTRY_DESTROY_DEF rv_mppc_disc_entry_destroy_fn

#define RV_MPPC_DISC_ENTRY_CREATE  RV_MPPC_STR_DEF(RV_MPPC_DISC_ENTRY_CREATE_DEF)
#define RV_MPPC_DISC_ENTRY_DESTROY RV_MPPC_STR_DEF(RV_MPPC_DISC_ENTRY_DESTROY_DEF)

#ifdef __cplusplus
extern "C" {
#endif

/* RV_CDEF_BEGIN */

struct rv_mppc_note_desc {
    // Checksum of the built disc.so: a SHA-256 truncated to 8 bytes (a deliberate
    // truncation, not a full digest) over the .text, .rodata and .data sections,
    // each prefixed with its own uint64 LE size. The shared implementation is
    // pdklib/rv_disc_hash (rv_disc_hash_compute computes the sum,
    // rv_disc_hash_magic_offset locates this field in the file through
    // .note.rv_mppc_ver).
    //
    // The .note.rv_mppc_ver note is itself excluded from the sum, which is what
    // lets the burner write the checksum here AFTER linking disc.so without
    // invalidating it: the bytes of this field take part in the computation
    // neither on write nor on check. The burner writes the sum here once the
    // build is done, and rv_pcloader::pre_dlopen_check recomputes and compares
    // it before dlopen.
    char magic[8];
    uint32_t version_major;
    uint32_t version_minor;
};

typedef struct rv_mppc_note_desc rv_mppc_note_desc;

typedef rv_de *(*rv_mppc_disc_create_fn)(void);
typedef void (*rv_mppc_disc_destroy_fn)(rv_de *disc);

/* RV_CDEF_END */

#ifdef __cplusplus
} /* extern "C" */
#endif

#ifdef __cplusplus
static_assert(sizeof(rv_mppc_note_desc) == 16);
#else
_Static_assert(sizeof(struct rv_mppc_note_desc) == 16, "rv_mppc_note_desc must be 16 bytes");
#endif

#ifdef __cplusplus

#define RV_MPPC_DISC_ENTRY_DEF(disc_class)                                                   \
    static int64_t rv_mppc_disc_entry_thunk_disc_initialize_(void *self_, rv_pdko *pdk)      \
    {                                                                                        \
        return static_cast<disc_class *>(self_)->disc_initialize(pdk);                       \
    }                                                                                        \
    static void rv_mppc_disc_entry_thunk_frame_update_(void *self_, float dt)                \
    {                                                                                        \
        static_cast<disc_class *>(self_)->frame_update(dt);                                  \
    }                                                                                        \
    static void rv_mppc_disc_entry_thunk_frame_render_(void *self_)                          \
    {                                                                                        \
        static_cast<disc_class *>(self_)->frame_render();                                    \
    }                                                                                        \
    static int rv_mppc_disc_entry_thunk_disc_release_(void *self_)                           \
    {                                                                                        \
        return static_cast<disc_class *>(self_)->disc_release() ? 1 : 0;                     \
    }                                                                                        \
    static void rv_mppc_disc_entry_thunk_disc_shutdown_(void *self_)                         \
    {                                                                                        \
        static_cast<disc_class *>(self_)->disc_shutdown();                                   \
    }                                                                                        \
    static const char *rv_mppc_disc_entry_thunk_disc_title_(void *self_)                     \
    {                                                                                        \
        return static_cast<disc_class *>(self_)->disc_title();                               \
    }                                                                                        \
    extern "C" __attribute__((visibility("default"))) rv_de *RV_MPPC_DISC_ENTRY_CREATE_DEF() \
    {                                                                                        \
        rv_de *de_ = new rv_de{                                                              \
            new disc_class(),                                                                \
            rv_mppc_disc_entry_thunk_disc_initialize_,                                       \
            rv_mppc_disc_entry_thunk_frame_update_,                                          \
            rv_mppc_disc_entry_thunk_frame_render_,                                          \
            rv_mppc_disc_entry_thunk_disc_release_,                                          \
            rv_mppc_disc_entry_thunk_disc_shutdown_,                                         \
            rv_mppc_disc_entry_thunk_disc_title_,                                            \
        };                                                                                   \
        return de_;                                                                          \
    }                                                                                        \
    extern "C" __attribute__((visibility("default"))) void RV_MPPC_DISC_ENTRY_DESTROY_DEF(   \
        rv_de *disc)                                                                         \
    {                                                                                        \
        delete static_cast<disc_class *>(disc->self);                                        \
        delete disc;                                                                         \
    }

// The built-in disc (src/rv_dmain) is linked into the console statically: it has
// no use for entry points, but it does need the table. The same set of thunks
// without new/delete — the caller creates and owns the disc object, and this
// only wraps it.
#define RV_MPPC_DISC_TABLE_DEF(disc_class, fn_name)                            \
    static int64_t fn_name##_thunk_disc_initialize_(void *self_, rv_pdko *pdk) \
    {                                                                          \
        return static_cast<disc_class *>(self_)->disc_initialize(pdk);         \
    }                                                                          \
    static void fn_name##_thunk_frame_update_(void *self_, float dt)           \
    {                                                                          \
        static_cast<disc_class *>(self_)->frame_update(dt);                    \
    }                                                                          \
    static void fn_name##_thunk_frame_render_(void *self_)                     \
    {                                                                          \
        static_cast<disc_class *>(self_)->frame_render();                      \
    }                                                                          \
    static int fn_name##_thunk_disc_release_(void *self_)                      \
    {                                                                          \
        return static_cast<disc_class *>(self_)->disc_release() ? 1 : 0;       \
    }                                                                          \
    static void fn_name##_thunk_disc_shutdown_(void *self_)                    \
    {                                                                          \
        static_cast<disc_class *>(self_)->disc_shutdown();                     \
    }                                                                          \
    static const char *fn_name##_thunk_disc_title_(void *self_)                \
    {                                                                          \
        return static_cast<disc_class *>(self_)->disc_title();                 \
    }                                                                          \
    static rv_de fn_name(disc_class *disc)                                     \
    {                                                                          \
        return rv_de{                                                          \
            disc,                                                              \
            fn_name##_thunk_disc_initialize_,                                  \
            fn_name##_thunk_frame_update_,                                     \
            fn_name##_thunk_frame_render_,                                     \
            fn_name##_thunk_disc_release_,                                     \
            fn_name##_thunk_disc_shutdown_,                                    \
            fn_name##_thunk_disc_title_,                                       \
        };                                                                     \
    }

#endif // __cplusplus

#endif // RV_PDK_DE_RV_DV_H
