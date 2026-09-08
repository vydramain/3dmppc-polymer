#include <cstdint>
#include <cstdio>

#include "pdk/cv/rv_cv.h"
#include "pdk/de/rv_de.h"
#include "pdk/de/rv_dv.h"
#include "pdk/rv_pdko.h"

namespace example_lua
{

namespace
{

// rv_cl_script_entry() and rv_cl_script_load() both return a chunk handle
// >= 0, so no handle value can also mean "nothing raised yet" - a sentinel is
// needed, the same way example-cpp.cpp uses video address 0 for "nothing
// allocated".
constexpr int64_t RV_EXAMPLE_LUA_NO_CHUNK = -1;

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
    const char *disc_title() const
    {
        return "example-lua";
    }

private:
    rv_pdko *pdk_ = nullptr;

    // The chunk rv_cl_script_entry() raised. The lua_State itself is never
    // kept here - it belongs to the console's script machine, and every hook
    // below re-derives it with rv_pdko_cl(pdk_), the same way example-cpp.cpp
    // re-derives rv_cv* each time rather than caching it.
    int64_t chunk_ = RV_EXAMPLE_LUA_NO_CHUNK;

    bool release_ = false;

    // Latched the first time a per-frame script_call fails, so frame_update
    // and frame_render stop forwarding into a script that already told us it
    // is broken, instead of re-reporting the same failure sixty times a
    // second.
    bool script_broken_ = false;
};

int64_t rv_dmain::disc_initialize(rv_pdko *pdk)
{
    pdk_ = pdk;

    rv_cv *cv = rv_pdko_cv(pdk_);
    rv_ca *ca = rv_pdko_ca(pdk_);
    rv_cio *cio = rv_pdko_cio(pdk_);

    // The contract itself does not require a script machine: a C++ disc gets
    // nullptr from rv_pdko_cl() and never calls a single rv_cl_* function.
    // This disc has no C++ fallback, though - every hook below exists only to
    // forward into Lua - so no machine here is a failure of THIS disc's own
    // design, not something the contract demands of every disc.
    rv_cl *cl = rv_pdko_cl(pdk_);
    if (!cl) {
        return RV_ERR_INVAL;
    }

    // "Raise the entry you know about" - the console already resolved and
    // verified [budget.pccl] script_entry against the archive while loading
    // the manifest, so the disc names nothing here, it only asks.
    chunk_ = rv_cl_script_entry(cl);
    if (chunk_ < 0) {
        return chunk_;
    }

    // Hand the script the controllers it will forward draw/sound/input calls
    // to, once the console binds hardware into the VM. Until then they arrive
    // as light userdata the script can only hold onto - see
    // scripts/example-lua.lua.
    rv_cl_stack_push_pointer(cl, cv);
    rv_cl_stack_push_pointer(cl, ca);
    rv_cl_stack_push_pointer(cl, cio);

    const int64_t call = rv_cl_script_call(cl, chunk_, "disc_initialize", 3, 0);
    if (call < 0) {
        // rv_cl_script_call already logged the Lua message itself, with the
        // chunk and hook name attached (see its doc in rv_cl.h) - naming
        // which of the disc's own hooks failed, and with what rv_err, is all
        // that is left to add here.
        std::fprintf(stderr, "example-lua: disc_initialize failed (rv_err %lld)\n",
            static_cast<long long>(call));
        return call;
    }

    return RV_OK;
}

void rv_dmain::frame_update(float dt)
{
    if (script_broken_) {
        return;
    }

    // Guaranteed non-null: disc_initialize already refused to start (above)
    // the one time rv_pdko_cl() could have come back nullptr, and the script
    // machine does not appear or vanish under a disc that is already running.
    rv_cl *cl = rv_pdko_cl(pdk_);

    rv_cl_stack_push_number(cl, dt);
    const int64_t call = rv_cl_script_call(cl, chunk_, "frame_update", 1, 1);
    if (call < 0) {
        // The pushed dt is already gone on this path too (rv_cl_script_call's
        // own contract), so there is nothing on the stack to drop here.
        std::fprintf(stderr, "example-lua: frame_update failed (rv_err %lld)\n",
            static_cast<long long>(call));
        script_broken_ = true;
        return;
    }

    // The script answers "should the disc stop" as frame_update's own return
    // value - that is release_ from here on, until frame_update runs again.
    rv_cl_value_boolean(cl, -1, &release_);
    rv_cl_stack_drop(cl, 1);
}

void rv_dmain::frame_render()
{
    if (script_broken_) {
        return;
    }

    // Guaranteed non-null - see frame_update above.
    rv_cl *cl = rv_pdko_cl(pdk_);
    rv_cv *cv = rv_pdko_cv(pdk_);
    const int64_t call = rv_cl_script_call(cl, chunk_, "frame_render", 0, 0);
    if (call < 0) {
        std::fprintf(stderr, "example-lua: frame_render failed (rv_err %lld)\n",
            static_cast<long long>(call));
        script_broken_ = true;
        return;
    }

    // The script only FILLS the frame (pdk.cv_frame_configure/cv_frame_put,
    // see scripts/example-lua.lua) - closing it stays in C++, the same shape
    // as example-cpp.cpp's own frame_render.
    rv_cv_frame_flush(cv);
}

// disc_release() never enters the VM (see the inline definition above) - the
// script already answered this as frame_update's own return value, and
// paying an entry into the machine for one boolean it told us a moment ago
// would be waste.

void rv_dmain::disc_shutdown()
{
    if (!pdk_) {
        return;
    }

    // chunk_ only ever leaves RV_EXAMPLE_LUA_NO_CHUNK once cl was confirmed
    // non-null in disc_initialize (above), so that same guarantee covers cl
    // here too - a second null check on it would just repeat the first one.
    if (chunk_ == RV_EXAMPLE_LUA_NO_CHUNK) {
        return;
    }

    rv_cl *cl = rv_pdko_cl(pdk_);
    const int64_t call = rv_cl_script_call(cl, chunk_, "disc_shutdown", 0, 0);
    if (call < 0) {
        // Shutdown proceeds either way - there is no later hook left to skip
        // to - but nothing was pushed on this path (rv_cl_script_call's own
        // contract), so there is nothing here to drop before freeing below.
        std::fprintf(stderr, "example-lua: disc_shutdown failed (rv_err %lld)\n",
            static_cast<long long>(call));
    }

    rv_cl_script_free(cl, chunk_);
    chunk_ = RV_EXAMPLE_LUA_NO_CHUNK;
}

} // namespace example_lua

RV_MPPC_DISC_ENTRY_DEF(example_lua::rv_dmain);
