#ifndef RV_PDK_DE_RV_DV_H
#define RV_PDK_DE_RV_DV_H

#include <stdint.h>

#include "pdk/cl/rv_cl.h"
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
#define RV_MPPC_VER_MINOR 3

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

#define RV_MPPC_DISC_DEF(disc_class)                                                         \
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

// The class RV_MPPC_DISC_CL_BASE_DEF builds below reaches the script chip through
// this accessor, declared here rather than pulled in via a full
// "pdk/rv_pdko.h" include - that header is the one that includes THIS one,
// for the version vocabulary above, and closing the loop the other way would
// only buy back a declaration this file can as easily repeat. Same reasoning
// as the forward-declared rv_pdko in pdk/de/rv_de.h.
extern "C" rv_cl *rv_pdko_cl(rv_pdko *o);

// A disc that hands its hooks to a Lua chunk instead of writing them in C++
// still has to make exactly six rv_cl calls in exactly one order for the
// console (src/rv_pconsole/cl/rv_pccl_luajit.cpp) and the disc to agree on
// what "calling a hook" means: raise the entry chunk and check the result,
// push the organizer pointer before every call into it, call each hook by
// its contract name with its contract arity, read frame_update's boolean
// result back and drop it, and free the chunk on shutdown. THAT is the
// contract - it is what makes a class a lua-flavoured rv_de - so it lives
// here beside RV_MPPC_DISC_DEF rather than in pdklib. rv_cl is already part
// of pdk (pdk/include/pdk/cl/rv_cl.h), so a disc using only this macro pulls
// in no pdklib code at all.
//
// class_name and title_literal are an ordinary identifier and an ordinary
// string literal - nothing more. This macro is a pdk CONTRACT, the kind of
// thing a disc's own .cpp writes by hand, so its interface has to read like
// an ordinary declaration, not like a text splice: no argument is ever a
// statement or an expression, and no argument ever needs to know this
// class's own member names. A disc that wants a title computed at runtime
// rather than a fixed literal writes its own class instead of using this
// macro - disc_title() is one line to override.
//
// This macro defines the class ONLY; it does not plant the entry points -
// RV_MPPC_DISC_DEF already exists for that and is called separately, right
// after it:
//
//   RV_MPPC_DISC_CL_BASE_DEF(my_disc, "my-game")
//   RV_MPPC_DISC_DEF(my_disc)
//
// Splitting the two calls is what lets the class defined here be inherited
// from before anything plants it - see pdklib's RV_MPPC_DISC_LUA_DEF, which
// derives its own class from this one to add a post-render flush, then
// plants the derived class instead of this one.
//
// frame_render() reports whether its rv_cl_script_call succeeded (rather
// than swallowing that the way disc_shutdown's does) because a class built
// on top of this one needs to know before it decides whether the frame is
// worth flushing - flushing itself is not part of this contract, see below.
#define RV_MPPC_DISC_CL_BASE_DEF(class_name, title_literal)                              \
    class class_name                                                                     \
    {                                                                                    \
    public:                                                                              \
        int64_t disc_initialize(rv_pdko *pdk)                                            \
        {                                                                                \
            pdk_ = pdk;                                                                  \
            rv_cl *cl = rv_pdko_cl(pdk_);                                                \
            chunk_ = rv_cl_script_entry(cl);                                             \
            if (chunk_ < 0) {                                                            \
                return chunk_;                                                           \
            }                                                                            \
            rv_cl_stack_push_pointer(cl, pdk_);                                          \
            const int64_t call = rv_cl_script_call(cl, chunk_, "disc_initialize", 1, 0); \
            if (call < 0) {                                                              \
                return call;                                                             \
            }                                                                            \
            return 0;                                                                    \
        }                                                                                \
        void frame_update(float dt)                                                      \
        {                                                                                \
            rv_cl *cl = rv_pdko_cl(pdk_);                                                \
            rv_cl_stack_push_number(cl, dt);                                             \
            rv_cl_stack_push_pointer(cl, pdk_);                                          \
            const int64_t call = rv_cl_script_call(cl, chunk_, "frame_update", 2, 1);    \
            if (call < 0) {                                                              \
                return;                                                                  \
            }                                                                            \
            rv_cl_value_boolean(cl, -1, &release_);                                      \
            rv_cl_stack_drop(cl, 1);                                                     \
        }                                                                                \
        bool frame_render()                                                              \
        {                                                                                \
            rv_cl *cl = rv_pdko_cl(pdk_);                                                \
            rv_cl_stack_push_pointer(cl, pdk_);                                          \
            const int64_t call = rv_cl_script_call(cl, chunk_, "frame_render", 1, 0);    \
            return call >= 0;                                                            \
        }                                                                                \
        bool disc_release() const                                                        \
        {                                                                                \
            return release_;                                                             \
        }                                                                                \
        void disc_shutdown()                                                             \
        {                                                                                \
            rv_cl *cl = rv_pdko_cl(pdk_);                                                \
            rv_cl_stack_push_pointer(cl, pdk_);                                          \
            rv_cl_script_call(cl, chunk_, "disc_shutdown", 1, 0);                        \
            rv_cl_script_free(cl, chunk_);                                               \
        }                                                                                \
        const char *disc_title() const                                                   \
        {                                                                                \
            return title_literal;                                                        \
        }                                                                                \
                                                                                         \
    protected:                                                                           \
        rv_pdko *pdk_ = nullptr;                                                         \
        int64_t chunk_ = -1;                                                             \
        bool release_ = false;                                                           \
    };

#endif // __cplusplus

#endif // RV_PDK_DE_RV_DV_H
