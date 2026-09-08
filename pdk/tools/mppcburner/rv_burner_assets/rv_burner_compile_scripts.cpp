#include "rv_burner_compile_scripts.hpp"

#include <cstddef>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

#include "lua.hpp"

namespace fs = std::filesystem;

namespace rv_pdktools
{

// The sink lua_dump() writes through. LuaJIT hands out the compiled chunk in
// pieces and calls this once per piece.
//
// `ud` is the untyped user pointer handed to lua_dump() — a C API cannot know
// our type, so it round-trips through void* and the cast back is unavoidable.
// `auto` would not help: it would deduce void*, which is exactly the type that
// cannot be appended to. `p` is one piece of bytecode, `sz` bytes of it, valid
// only until this call returns — hence the copy.
//
// Returning non-zero would abort the dump; there is nothing here that can fail.
static int bytecode_writer(lua_State *, const void *p, size_t sz, void *ud)
{
    auto *sink = static_cast<std::vector<std::byte> *>(ud);

    const std::size_t old_size = sink->size();
    sink->resize(old_size + sz);
    std::memcpy(sink->data() + old_size, p, sz);

    return 0;
}

} // namespace rv_pdktools

int rv_pdktools::compile_simple_script(
    const std::string &lua_path,
    const std::string &out_path,
    std::string &error)
{
    // --- a vm to compile in ---
    //
    // Fresh per script and never run: the burner compiles, it does not execute.
    // No standard library is opened, so a script cannot reach the burner's
    // filesystem while being turned into bytecode.
    lua_State *L = luaL_newstate();
    if (L == nullptr) {
        error = "cannot initialize a local lua vm";
        return 1;
    }

    // --- parse ---
    //
    // luaL_loadfile parses the file and leaves the result on the VM stack: a
    // callable function on success, an error message on failure. Nothing runs.
    const int load_status = luaL_loadfile(L, lua_path.c_str());
    if (load_status != 0) {
        // Never NULL: every failure path in lj_load.c leaves a string here.
        error = lua_tostring(L, -1);
        lua_close(L);
        return 1;
    }

    // --- dump ---
    //
    // lua_dump serialises the function on top of the stack into the bytes the VM
    // would otherwise have to re-parse. It does not return a buffer: it calls
    // the writer above, repeatedly, and `sink` is what accumulates.
    std::vector<std::byte> sink;
    const int dump_status = lua_dump(L, &bytecode_writer, &sink);

    // `sink` owns the bytecode from here on. Closing the VM at the one point it
    // stops being needed means no exit path below can forget to.
    lua_close(L);

    if (dump_status != 0) {
        error = "luajit could not dump '" + lua_path + "' (status " +
            std::to_string(dump_status) +
            "): the loaded chunk is not a lua function, or the vm failed while writing bytecode";
        return 1;
    }

    // --- write ---

    std::ofstream out(out_path, std::ios::binary | std::ios::trunc);
    if (!out) {
        error = "cannot open '" + out_path + "' for writing";
        return 1;
    }

    out.write(reinterpret_cast<const char *>(sink.data()),
        static_cast<std::streamsize>(sink.size()));

    // Closed explicitly: the tail of the buffer reaches the disk here, and a
    // failure at that moment would otherwise be swallowed by the destructor.
    out.close();
    if (!out) {
        error = "short write on '" + out_path + "'";
        // A truncated .luac must not survive looking like a compiled script.
        // Same rule the archive writer follows for a partial .mppcdisc: a
        // half-written artifact is removed rather than left to be picked up.
        std::error_code removal_failed;
        fs::remove(out_path, removal_failed);
        return 1;
    }

    return 0;
}

int rv_pdktools::compile_scripts(
    const archive_plan &plan,
    const fs::path &disc_dir,
    std::string &error)
{
    // Two roots meet here. `source` is relative to the disc directory, where the
    // author's .lua lives; `payload` is already absolute, under the build tree,
    // where its bytecode goes. Only the first needs joining.
    for (std::size_t i = plan.first_script; i < plan.first_script + plan.script_count; ++i) {
        const archive_item &item = plan.items[i];

        std::string lua_error;
        if (compile_simple_script((disc_dir / item.source).string(), item.payload,
                lua_error) != 0) {
            error = "cannot compile '" + item.source + "': " + lua_error;
            return 1;
        }
    }

    return 0;
}
