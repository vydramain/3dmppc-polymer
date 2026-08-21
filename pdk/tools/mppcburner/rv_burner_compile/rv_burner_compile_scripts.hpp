#pragma once

#include <string>

namespace rv_pdktools
{

bool rv_burner_compile_scripts(
	const std::string &lua_path,
	const std::string &out_path,
	std::string &error);

} // namespace rv_pdktools
