#pragma once

#include <cstddef>
#include <cstdlib>
#include <string>
#include <string_view>

#include "rv_burner_schema.hpp"

namespace rv_pdktools
{

std::size_t edit_distance(std::string_view a, std::string_view b);

std::string suggest(std::string_view word, const std::string_view *candidates, std::size_t count);

std::string suggest_section(std::string_view word, const section_spec *burn_sections, std::size_t burn_sections_size);

} // namespace rv_pdktools
