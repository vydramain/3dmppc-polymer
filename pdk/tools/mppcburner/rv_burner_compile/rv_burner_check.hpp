#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "rv_burner_manifest/rv_burner_manifest.hpp"

namespace rv_pdktools
{

// Resolve every manifest `sources` entry against the disc directory, and every
// `include_dirs` entry too — refusing any that leaves the disc. Returns 0 on
// success and 1 on refusal; `error` is a fragment the caller prefixes.
int rv_burner_check_sources_outside_disc(
	const rv_burner_manifest &manifest,
	const std::filesystem::path &disc_dir,
	const std::vector<std::string> &sources,
	std::vector<std::string> &absolute_includes,
	std::vector<std::string> &absolute_sources,
	std::string &error);

} // namespace rv_pdktools
