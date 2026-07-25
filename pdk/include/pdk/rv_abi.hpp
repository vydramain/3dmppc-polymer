#pragma once

#include "pdk/de/rv_de.hpp"

// The BINARY boundary between a console and a disc: the two functions a
// `.mppcdisc` exports and the version number both sides must agree on.
//
// Everything else in the PDK is a source-level contract — headers both sides
// compile against. This file is different: it describes bytes and calling
// conventions across a `dlopen`, where the two sides were compiled separately,
// possibly by different compilers, possibly years apart.
//
// WHY `extern "C"` AND NOT A C++ FUNCTION. The C++ ABI is not stable across
// compilers, compiler versions, standard libraries or even optimization flags:
// name mangling, vtable layout, exception propagation and RTTI representation
// all differ. A C++ symbol is therefore a poor gate between two independently
// built binaries. `extern "C"` has none of that freedom — one symbol name, one
// calling convention — so it is a narrow, predictable door. Once the console
// holds the returned rv_de*, both sides are inside ONE process again and the
// vtable is internally consistent, which is why a C++ object may cross here
// even though a C++ function may not.
//
// The disc still has to be built against the SAME PDK headers as the console:
// this gate protects against toolchain differences, not against a disc compiled
// against a different rv_cv. That is what RV_ABI_VERSION is for.

namespace rv_pdk {

// Bumped whenever anything a disc can observe changes shape: a struct in pdk/
// gains or loses a field, an enumerator is renumbered, a virtual is inserted in
// the middle of a controller. NOT bumped for comment or documentation changes.
//
// A disc records the version it was burned against in its manifest, and the
// console refuses to load a disc that does not match — refusing is the whole
// point. The alternative is a disc that loads and then reads a struct with the
// wrong layout, which does not fail: it produces garbage geometry, silent
// corruption, or a crash three minutes into play with no clue as to the cause.
inline constexpr int RV_ABI_VERSION = 1;

// The names the console looks up with dlsym. They are spelled out here so the
// two sides cannot drift: the disc exports them through RV_DISC_EXPORT below,
// and the loader asks for exactly these strings.
inline constexpr const char* RV_ABI_SYMBOL_CREATE = "mppc_disc_create";
inline constexpr const char* RV_ABI_SYMBOL_DESTROY = "mppc_disc_destroy";

// The factory's signature, as the console sees it after dlsym.
//
// The disc returns a heap-allocated rv_de it constructed itself, or nullptr if
// `abi_version` is not the one it was built for. That nullptr is the second half
// of the handshake: the console already compared the manifest's version, but a
// manifest is text and can be edited, whereas this check is compiled into the
// binary that would actually run.
using rv_disc_create_fn = rv_de* (*)(int abi_version);

// The matching destructor. It MUST be the disc's own, never the console's
// `delete`: the disc allocated the object with its allocator, out of its heap,
// and only its code knows the complete type. The console calls this, then
// dlclose — in that order, or the destructor's code is gone before it runs.
using rv_disc_destroy_fn = void (*)(rv_de* disc);

}  // namespace rv_pdk

// What a disc writes to export its entry point. One line at the bottom of the
// game's own translation unit:
//
//     RV_DISC_EXPORT(rv_dmain)
//
// `default` visibility is explicit because a disc is expected to be compiled
// with -fvisibility=hidden (nothing else in it should be reachable by dlsym);
// these two symbols are the deliberate exception.
#define RV_DISC_EXPORT(disc_class)                                                          \
    extern "C" __attribute__((visibility("default"))) rv_pdk::rv_de* mppc_disc_create(   \
        int abi_version) {                                                                  \
        if (abi_version != rv_pdk::RV_ABI_VERSION) return nullptr;                       \
        return new disc_class();                                                            \
    }                                                                                       \
    extern "C" __attribute__((visibility("default"))) void mppc_disc_destroy(               \
        rv_pdk::rv_de* disc) {                                                           \
        delete disc;                                                                        \
    }
