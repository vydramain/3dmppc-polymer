#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

#include "rv_build.hpp"

namespace rv_pdktools
{

// Compile the scripts already PLANNED into `items[first_script ..)` — the same
// plan-then-work order the assets stage uses, so a name collision is caught
// before one .luac can overwrite another.
//
// For each of those items `source` is the .lua the author wrote, relative to
// `disc_dir`, and `payload` is the absolute path its bytecode is written to.
// Returns 0, or 1 with `error` set.
int rv_burner_compile_scripts(
	const std::vector<rv_archive_item> &items,
	std::size_t first_script,
	const std::filesystem::path &disc_dir,
	std::string &error);

// Compile one .lua file to a .luac bytecode file. `lua_path` is READ,
// `out_path` is created and truncated.
int rv_burner_compile_simple_script(
	const std::string &lua_path,
	const std::string &out_path,
	std::string &error);

} // namespace rv_pdktools
