#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "rv_burner_manifest/rv_burner_manifest.hpp"
#include "rv_burner_options.hpp"

namespace rv_pdktools
{

// Render the CMakeLists.txt of the generated disc project. Pure text in, pure
// text out: no filesystem, no child processes, so a harness can read what the
// tool would have written without a disc directory.
std::string rv_cmake_project_text(
	const rv_burner_manifest &manifest,
	const std::string &pdk_dir,
	const std::string &pdklib_dir,
	const std::vector<std::string> &absolute_includes,
	const std::vector<std::string> &absolute_sources);

// Materialise the generated project on disk: CMakeLists.txt and the one
// translation unit that stamps the disc version into it.
int rv_burner_create_cmakelists(
	const rv_burner_options &options,
	const rv_burner_manifest &manifest,
	const std::filesystem::path &project_dir,
	const std::vector<std::string> &absolute_includes,
	const std::vector<std::string> &absolute_sources,
	std::string &error);

// Run `cmake -G Ninja` over the generated project. cmake and ninja are run-time
// dependencies of the burner, not build-time ones.
int rv_burner_prepare_build_configure_cmake(
	const std::filesystem::path &binary_dir,
	const std::filesystem::path &project_dir,
	std::string &error);

} // namespace rv_pdktools
