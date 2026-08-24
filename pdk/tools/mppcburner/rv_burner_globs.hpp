#pragma once

#include <filesystem>
#include <vector>

namespace rv_pdktools
{

bool has_wildcard(std::string_view component);

bool wildcard_match(std::string_view pattern, std::string_view text);

std::vector<std::string> split_components(const std::string &pattern);

bool rv_glob_expand(
	const std::filesystem::path &root,
	const std::vector<std::string> &patterns,
	std::vector<std::string> &out,
	std::string &error);

} // namespace rv_pdktools
