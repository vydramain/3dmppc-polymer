#pragma once

#include <cstddef>
#include <filesystem>
#include <string>

#include "rv_burner_options.hpp"

namespace rv_pdktools
{

// [2/4] compile: drive `cmake --build` over the generated project and confirm it
// produced disc.so. Returns 0 on success and 1 on any refusal — the exit-code
// contract the other phase functions use. The child's own diagnostic goes to
// stderr verbatim; `error` carries only this tool's one-line summary.
int rv_burner_compile_sources(
	const rv_burner_options &options,
	const std::filesystem::path &binary_dir,
	std::size_t source_count,
	std::string &error);

} // namespace rv_pdktools
