#pragma once

#include <cstddef>
#include <string>
#include <string_view>

#include "rv_manifest_schema.hpp"

namespace rv_pdklib
{

std::size_t edit_distance(std::string_view a, std::string_view b);

std::string suggest(std::string_view word, const std::string_view *candidates, std::size_t count);

std::string suggest_section(std::string_view word, const rv_manifest_section_spec *sections,
	std::size_t section_count);

std::string suggest_key(std::string_view word, const rv_manifest_key_spec *keys,
	std::size_t key_count);

} // namespace rv_pdklib
