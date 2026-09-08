// ─── TASK ──────────────────────────────────────────────────────────────────
// Comments written by Claude (claude-opus-5) as teaching instructions.
// You write the code. The file is empty on purpose.
// ──────────────────────────────────────────────────────────────────────────────
//
// THE ONLY file in the whole project that includes "lua.hpp".
// If it shows up anywhere else, the PDK boundary has leaked.
//
//
// TODO(1). CONSTRUCTOR
//
//   luaL_newstate() — create the VM. It can return nullptr (out of memory);
//   the console must survive that, not dereference it.
//
//   Next, open the libraries. luaL_openlibs() opens EVERYTHING, including io
//   and os, that is, it gives the disc script access to the filesystem and to
//   os.exit(). For a fantasy console this is a hole: the disc must only reach
//   the outside world through controllers (rv_cd, rv_cm). Open the libraries
//   one at a time — luaopen_base / luaopen_string / luaopen_math /
//   luaopen_table — and do not open io/os. This is a deliberate decision,
//   write it down in a comment nearby.
//
//   lua_atpanic(L_, ...) — install your own handler. By default, if an error
//   happens OUTSIDE a pcall, Lua calls abort() and the console dies without a
//   single line in the log.
//
// TODO(2). DESTRUCTOR
//
//   lua_close(L_) — shuts down the VM and runs the finalizers. Check for
//   nullptr: the constructor might have failed.
//
//
// TODO(3). script_load(bytecode, size, name)
//
//   Order, and what is on the stack after each step (the stack starts
//   empty):
//
//     validate the arguments        -> RV_ERR_INVAL if nullptr / size <= 0
//     luaL_loadbuffer(L_, ...)      -> [chunk_fn]   or 0 and [errmsg] on error
//     lua_pcall(L_, 0, 0, 0)        -> []           the body has run, globals exist
//     lua_getglobal("frame_update") -> [fn or nil]
//     lua_isfunction(L_, -1)        -> remember whether the hook is present
//     luaL_ref(L_, LUA_REGISTRYINDEX) -> []         pops off the stack, returns an int
//     ... same for the remaining hooks
//     return the handle
//
//   The THIRD line is the most important and the only non-obvious one.
//   luaL_loadbuffer only COMPILES the chunk into a function, the file's body
//   is not executed at that point. The globals frame_update / frame_render
//   are created as a SIDE EFFECT of executing the body — that is exactly how
//   Lua differs from a .so, where symbols simply sit in a table and are found
//   by lookup.
//
//   Check yourself: lua_gettop(L_) on entry and on exit must be equal.
//
//   Errors from luaL_loadbuffer and lua_pcall return a nonzero code and put
//   the text on top of the stack. Log it (pdklib/rv_logs/rv_logs.hpp), pop it
//   with lua_pop and return RV_ERR_IO. The console must not be allowed to
//   crash because of a broken script.
//
//
// TODO(4). script_call(chunk, fname, argc, retc)
//
//   Here is a trap that is only visible if you draw the stack.
//
//   The disc has ALREADY pushed argc arguments, they sit on top. But
//   lua_pcall requires the order [function][arg1][arg2]. That is, the
//   function has to be inserted UNDER the arguments already there, not on top
//   of them.
//
//   It is done like this: lua_rawgeti puts the function on top, and
//   lua_insert(L_, -argc-1) moves it down, under the arguments. Work this
//   index out on paper before you write the code — a mistake here produces
//   "attempt to call a number value" and looks inexplicable.
//
//   An alternative that removes this trap entirely: let the disc not push the
//   arguments beforehand, and have script_call take them from its own
//   parameters instead. Then group B in the contract is not needed at all.
//   Think about what you want and write the choice down in a comment here.
//
//   On error: RV_ERR_IO, LEAVE the error text on the stack (the disc will
//   read it via value_string). Note that this breaks the stack balance —
//   document that after an error the disc must call stack_drop.
//
//
// TODO(5). value_* and stack_push_*
//
//   Direct wrappers: lua_pushnumber, lua_tonumber, lua_toboolean,
//   lua_tolstring.
//
//   One spot needs attention — value_string. lua_tolstring returns a pointer
//   INTO a string owned by the Lua garbage collector. It must not be handed
//   out: memcpy into the disc's buffer, return the number of bytes written.
//   If the buffer is too short — RV_ERR_INVAL, not a silent truncation.
//
//
// TODO(6). BUILD
//
//   The root CMakeLists.txt already links libluajit into the 3dmppc target,
//   and the .cpp is picked up by the glob — no need to touch the build.
//
//
// TODO(7). BOUNDARY WITH THE CONTRACT
//
//   The contract has become plain C, so besides the class methods you need a
//   tail section in the file with fifteen extern "C" definitions of rv_cl_* —
//   they are what the disc actually sees. Reference — the end of
//   rv_pccv.cpp. Casting the handle to rv_pccl* is legitimate for the same
//   reason as with its neighbors: exactly one implementation of each
//   controller lives in the process.
//
//   And one line in rv_pconsole.cpp: rv_pdko_cl() returns nullptr for now.
