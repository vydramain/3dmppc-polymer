
#include "rv_burner_compile_scripts.hpp"

#include <cstddef>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <system_error>
#include <vector>

#include "lua.hpp"

namespace fs = std::filesystem;

static int custom_lua_writer(lua_State *, const void *p, size_t sz, void *ud)
{
	auto *sink_v = static_cast<std::vector<std::byte> *>(ud);
	const std::size_t sz_old = sink_v->size();
	sink_v->resize(sz_old + sz);
	std::memcpy(sink_v->data() + sz_old, p, sz);
	return 0;
}

int rv_pdktools::rv_burner_compile_simple_script(
	const std::string &lua_path,
	const std::string &out_path,
	std::string &error)
{
	lua_State *L = luaL_newstate();
	if (L == nullptr) {
		error = "cannot initialize a local lua vm";
		return 1;
	}

	int loadf_r = luaL_loadfile(L, lua_path.c_str());
	if (loadf_r != 0) {
		// Never NULL: every failure path in lj_load.c leaves a string on the stack.
		error = lua_tostring(L, -1);
		lua_close(L);
		return 1;
	}

	std::vector<std::byte> sink;

	int dump_r = lua_dump(L, &custom_lua_writer, &sink);

	// `sink` owns the bytecode from here on. Closing the VM at the one point it
	// stops being needed means no exit path below can forget to.
	lua_close(L);

	if (dump_r < 0) {
		error = "internal error: the bytecode writer returned " + std::to_string(dump_r) +
			" while dumping '" + lua_path + "'; a lua_Writer may only return 0 here";
		return 1;
	}
	if (dump_r > 0) {
		error = "luajit could not dump '" + lua_path + "' (status " + std::to_string(dump_r) +
			"): the loaded chunk is not a lua function, or the vm failed while writing bytecode";
		return 1;
	}

	std::ofstream out(out_path, std::ios::binary | std::ios::trunc);
	if (!out) {
		error = "cannot open '" + out_path + "' for writing";
		return 1;
	}

	out.write(reinterpret_cast<const char *>(sink.data()), sink.size());

	// Closed explicitly: the tail of the buffer reaches the disk here, and a
	// failure at that moment would otherwise be swallowed by the destructor.
	out.close();
	if (!out) {
		error = "short write on '" + out_path + "'";
		// A truncated .luac must not survive looking like a compiled script —
		// the rule rv_zipwrite.hpp states for a partial archive applies here too.
		std::error_code removal_failed;
		std::filesystem::remove(out_path, removal_failed);
		return 1;
	}

	return 0;
}

int rv_pdktools::rv_burner_compile_scripts(
	const std::vector<rv_pdktools::rv_archive_item> &items,
	std::size_t first_script,
	const fs::path &disc_dir,
	std::string &error)
{
	// Two different directories meet here and must not be confused: `source` is
	// relative to disc_dir, where the author's .lua files live, while `payload`
	// is already the absolute path under project_dir/scripts/ that the caller
	// planned for the bytecode.
	for (std::size_t i = first_script; i < items.size(); ++i) {
		const rv_pdktools::rv_archive_item &item = items[i];
		std::string lua_error;
		if (rv_burner_compile_simple_script(
				(disc_dir / item.source).string(),
				item.payload,
				lua_error) != 0) {
			error = "cannot compile '" + item.source + "': " + lua_error;
			return 1;
		}
	}

	return 0;
}
