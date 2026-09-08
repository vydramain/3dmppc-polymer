#pragma once

// ─── TASK ──────────────────────────────────────────────────────────────────
// Comments written by Claude (claude-opus-5) as teaching instructions.
// You write the code. The file is empty on purpose.
// ──────────────────────────────────────────────────────────────────────────────
//
// This is the CONSOLE SIDE of the pdk/cl/rv_cl.h contract — the same "machine
// 2 + 3" inside the hardware. The counterpart of rv_pccv for rv_cv: the
// contract is opaque, the concrete implementation lives here.
//
// ATTENTION: the contract is now PLAIN C. rv_cl is not an abstract class but
// an opaque type plus fifteen free functions rv_cl_*. Nothing to inherit and
// nothing to override: rv_pccl is an ordinary class, and the link to the
// contract is made by the rv_cl_* definitions at the end of rv_pccl.cpp, which
// cast the handle to rv_pccl*.
// Look at how this is done at the end of rv_pccv.cpp, and repeat it.
//
//
// WHAT TO INCLUDE
//
//   "pdk/cl/rv_cl.h" — contract declarations, mandatory.
//   <cstdint>, <vector>, <string> — as needed.
//
//   lua.hpp is NOT needed here, even though the class holds a lua_State*. It
//   is enough to forward-declare at the top of the file:  struct lua_State;
//   Declaring a pointer to an incomplete type is fine; the full definition is
//   only required where it is dereferenced — that is, in the .cpp. The rule
//   is simple: lua.hpp is included by exactly one file in the whole project,
//   rv_pccl.cpp.
//
//
// WHAT TO DECLARE
//
// TODO(1). class rv_pccl — WITHOUT inheritance, the contract is no longer a
//   class.
//
// TODO(2). Field lua_State *L_ = nullptr — THE WHOLE virtual machine.
//   The constructor creates it, the destructor shuts it down. There must be
//   no "turn on the VM" methods in the contract, and there never should be:
//   hardware is not switched on, it is already on.
//
// TODO(3). Forbid copying: rv_pccl(const rv_pccl &) = delete and the
//   assignment operator too. Otherwise a copy would close someone else's
//   lua_State in its destructor, while the original keeps using it — a
//   double free.
//   Look at how this is done in rv_pchost, and repeat it.
//
// TODO(4). The chunk table — the thing that turns a handle into something
//   Lua-ish. Inside Lua a function does not live at an address, it lives by a
//   REFERENCE IN THE REGISTRY: luaL_ref puts the value into the service
//   table LUA_REGISTRYINDEX and returns an int. As long as the reference is
//   alive, the garbage collector will not take the function away — that is
//   "ownership" from the C++ side.
//
//   So one chunk corresponds to several references: the chunk function
//   itself plus one reference per hook found. Set up a struct for this and
//   keep them in std::vector — the handle is then simply an index into it.
//   Decide for yourself whether handle == index (then 0 is a valid chunk) or
//   index + 1 (then 0 is free for "no chunk", like the video memory
//   addresses in example-cpp.cpp).
//
// TODO(5). Declare a method for each function of the contract — the same
//   names without the rv_cl_ prefix (script_load, script_call,
//   stack_push_number, ...). No override: there is nothing left to override.
//
//   Not a single extra PUBLIC method: anything not in the contract, the disc
//   will never see anyway — it only holds an rv_cl*, which does not even
//   have a type definition.
//
// TODO(6). At the end of rv_pccl.cpp — fifteen definitions of the form
//
//     extern "C" int64_t rv_cl_stack_push_number(rv_cl *cl, double value)
//     {
//         return reinterpret_cast<rv_3dmppc::rv_pccl *>(cl)->stack_push_number(value);
//     }
//
//   Take the signatures verbatim from pdk/cl/rv_cl.h. Until they exist, these
//   fifteen symbols are absent from the executable — checked via
//   `nm -D build/3dmppc | grep rv_cl_`.
