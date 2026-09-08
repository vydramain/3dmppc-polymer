#include <cstdint>

#include "pdk/de/rv_de.h"
#include "pdk/de/rv_dv.h"
#include "pdk/rv_pdko.h"

namespace example_lua
{

namespace
{

struct rv_example_lua_texheader {
    uint16_t version;
};

} // namespace

class rv_dmain
{
public:
    int64_t disc_initialize(rv_pdko *pdk);
    void frame_update(float dt);
    void frame_render();
    int disc_release() const
    {
        return release_;
    }
    void disc_shutdown();
    const char *disc_tilte() const
    {
        return "example-lua";
    };

private:
    rv_pdko *pdk_ = nullptr;
    bool release_ = false;

    // TODO(Claude instruction, code is yours). One field: the handle of the
    // chunk returned by rv_cl_script_load(). Initialize it with the "no
    // chunk" value.
    //
    // lua_State is NOT stored here and not created here: the VM belongs to
    // the console, the disc only asks for it via rv_pdko_cl(pdk_). Same
    // principle as with video memory — example-cpp.cpp holds addresses
    // (addr_texels_), not the buffer itself.
};

// ─── TASK: hook bodies ────────────────────────────────────────────────────────
// Comments written by Claude (claude-opus-5). The code is yours.
//
// Right now the class declares methods but there isn't a single definition —
// this file won't link. Below is what should be in each body.
//
//
// TODO(1). TYPO, fix it first: the method is called disc_tilte, but the
//   RV_MPPC_DISC_ENTRY_DEF macro (pdk/de/rv_dv.h) looks up exactly
//   disc_title when instantiating — a free member function it wraps in a
//   thunk. rv_de is no longer a base class, so the class won't "stay
//   abstract"; instead RV_MPPC_DISC_ENTRY_DEF(example_lua::rv_dmain) at the
//   end of the file won't compile: the compiler won't find disc_title() on
//   disc_class. Rename the method to disc_title.
//
//
// TODO(2). ANOTHER TYPO, fix it second: the scripts directory is called
//   sciprts/, but disc.toml asks for "scripts/*.lua". The glob won't match,
//   the burner will silently pick up zero scripts, and you'll go looking for
//   a bug in Lua that isn't there. Rename the directory.
//
//
int64_t rv_dmain::disc_initialize(rv_pdko *pdk)
{
}
//
// TODO(3). disc_initialize(pdk)
//
//   Order:
//     pdk_ = pdk;
//     grab the controllers: rv_pdko_cd(pdk_), rv_pdko_cl(pdk_); check both
//       for nullptr
//     read "example-lua.luac" from the medium — read_asset() in
//       example-cpp.cpp does exactly this (rv_cd_asset_open ->
//       rv_cd_asset_size -> rv_cd_asset_read), use it as a template; the
//       file name is whatever the burner put into the archive
//     chunk_ = rv_cl_script_load(cl, bytes.data(), bytes.size(), "example-lua")
//     if chunk_ < 0 — return a negative rv_err, NOT RV_OK
//
//   THIS LINE with script_load is exactly "the disc shows the console where
//   the bytecode is". No manifest declaration is needed for this: the disc
//   doesn't claim it has scripts — it just hands over the bytes.
//
//   The bytecode buffer can be released after script_load: the console has
//   already copied everything it needs into the VM. Just like
//   rv_cv_video_asset_write copies texels.
//
//
void rv_dmain::frame_update(float dt)
{
}
//
// TODO(4). frame_update(dt)
//
//   rv_cl_stack_push_number(cl, dt), then
//   rv_cl_script_call(cl, chunk_, "frame_update", 1, 1).
//   Read the result (does the script want to shut down) into release_ and
//   pop it off the stack.
//
//   If script_call returned an error — log it ONCE and remember with a flag
//   that the hook is broken, otherwise you'll be printing the same thing
//   sixty times a second.
//
//
void rv_dmain::frame_render()
{
}
//
// TODO(5). frame_render()
//
//   rv_cl_script_call(cl, chunk_, "frame_render", 0, 0).
//
//   IMPORTANT: rv_cv_frame_flush(cv) still calls C++, not the script. Until
//   Lua can draw (the reverse direction — the console binding hardware into
//   the VM — isn't designed yet), the script can only compute and print via
//   print(). That's fine for a first step: first make sure the bytecode
//   actually executes, and only then think about drawing from Lua.
//
//
void rv_dmain::disc_shutdown()
{
}
//
// TODO(6). disc_shutdown()
//
//   rv_cl_script_free(cl, chunk_) and zero out the field. The last moment
//   the facade is still alive — after it returns the console is free to
//   unload the disc's code entirely.
//
//
// TODO(7). Last line of the file — RV_MPPC_DISC_ENTRY_DEF(example_lua::rv_dmain);
//   Without it there are no exported symbols in disc.so, and the console
//   won't find the disc via dlsym. See example-cpp.cpp and pdk/de/rv_dv.h.
//
//
// WORK ORDER. Don't write everything at once. The first milestone is seeing
// "Hello from example lua!" in the terminal from scripts/example-lua.lua.
// TODO(3) and a single script_load are enough for that: the line is printed
// by the chunk's BODY, i.e. already at the lua_pcall step inside
// script_load, before any hooks.

} // namespace example_lua

RV_MPPC_DISC_ENTRY_DEF(example_lua::rv_dmain);
