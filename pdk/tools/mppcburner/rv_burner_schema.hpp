#pragma once

#include <cstddef>
#include <string_view>

#include "rv_burner_manifest_value.hpp"

// --- the schema ---------------------------------------------------------------
//
// The static half of the symbol table: the accepted spelling of every section
// and key, and the kind of value each key takes. Semantic analysis rejects
// anything absent from these tables AND spells its "did you mean" out of them,
// so a key added to rv_manifest must be added here or it stops being accepted —
// which is the failure mode we want, not the reverse.

namespace rv_pdktools
{

// Every section and key is named once here. Tables below refer to these
// constants rather than to positions, so reordering cannot silently rebind.

constexpr std::string_view rv_burner_section_disc = "disc";
constexpr std::string_view rv_burner_section_build = "build";
constexpr std::string_view rv_burner_section_scripts = "scripts";
constexpr std::string_view rv_burner_section_assets = "assets";
constexpr std::string_view rv_burner_section_textures = "textures";
constexpr std::string_view rv_burner_section_budget = "budget";

// Section ['disc']
constexpr std::string_view rv_burner_key_disc_id = "id";
constexpr std::string_view rv_burner_key_disc_title = "title";

// Section ['build']
constexpr std::string_view rv_burner_key_build_sources = "sources";
constexpr std::string_view rv_burner_key_build_defines = "defines";
constexpr std::string_view rv_burner_key_build_include_dirs = "include_dirs";

// Section ['scripts']
constexpr std::string_view rv_burner_key_scripts_sources = "sources";

// Section ['assets']
constexpr std::string_view rv_burner_key_assets_files = "files";

// Section ['textures']
constexpr std::string_view rv_burner_key_textures_files = "files";
constexpr std::string_view rv_burner_key_textures_format = "format";

// Section ['budget']
constexpr std::string_view rv_burner_key_budget_texture_max_width = "texture_max_width";
constexpr std::string_view rv_burner_key_budget_texture_max_height = "texture_max_height";
constexpr std::string_view rv_burner_key_budget_video_memory_size = "video_memory_size";

struct rv_burner_key_spec {
	std::string_view name;
	rv_burner_value_kind kind;
};

struct rv_burner_section_spec {
	std::string_view name;
	const rv_burner_key_spec *keys;
	std::size_t key_count;
};

constexpr rv_burner_key_spec rv_burner_disc_keys[] = {
	{ rv_burner_key_disc_id, rv_burner_value_kind::string },
	{ rv_burner_key_disc_title, rv_burner_value_kind::string },
};

constexpr rv_burner_key_spec rv_burner_build_keys[] = {
	{ rv_burner_key_build_sources, rv_burner_value_kind::array },
	{ rv_burner_key_build_defines, rv_burner_value_kind::array },
	{ rv_burner_key_build_include_dirs, rv_burner_value_kind::array },
};

constexpr rv_burner_key_spec rv_burner_scripts_keys[] = {
	{ rv_burner_key_scripts_sources, rv_burner_value_kind::array },
};

constexpr rv_burner_key_spec rv_burner_assets_keys[] = {
	{ rv_burner_key_assets_files, rv_burner_value_kind::array },
};

constexpr rv_burner_key_spec rv_burner_textures_keys[] = {
	{ rv_burner_key_textures_files, rv_burner_value_kind::array },
	{ rv_burner_key_textures_format, rv_burner_value_kind::string },
};

constexpr rv_burner_key_spec rv_burner_budget_keys[] = {
	{ rv_burner_key_budget_texture_max_width, rv_burner_value_kind::integer },
	{ rv_burner_key_budget_texture_max_height, rv_burner_value_kind::integer },
	{ rv_burner_key_budget_video_memory_size, rv_burner_value_kind::integer },
};

constexpr rv_burner_section_spec rv_burner_sections[] = {
	{ rv_burner_section_disc, rv_burner_disc_keys, std::size(rv_burner_disc_keys) },
	{ rv_burner_section_build, rv_burner_build_keys, std::size(rv_burner_build_keys) },
	{ rv_burner_section_scripts, rv_burner_scripts_keys, std::size(rv_burner_scripts_keys) },
	{ rv_burner_section_assets, rv_burner_assets_keys, std::size(rv_burner_assets_keys) },
	{ rv_burner_section_textures, rv_burner_textures_keys, std::size(rv_burner_textures_keys) },
	{ rv_burner_section_budget, rv_burner_budget_keys, std::size(rv_burner_budget_keys) },
};

// The spellings rv_manifest_validate accepts for [textures] format; they match
// the rv_texfmt enumerators of pdk/cv/rv_texture.hpp one for one.
constexpr std::string_view rv_burner_approved_texture_formats[] = { "idx4", "idx8", "direct15" };

inline const rv_burner_section_spec *rv_burner_sections_get(std::string_view name)
{
	for (const rv_burner_section_spec &s : rv_burner_sections) {
		if (s.name == name) {
			return &s;
		}
	}
	return nullptr;
}

inline const rv_burner_key_spec *rv_burner_keys_get(const rv_burner_section_spec &section,
	std::string_view name)
{
	for (std::size_t i = 0; i < section.key_count; ++i) {
		if (section.keys[i].name == name) {
			return &section.keys[i];
		}
	}
	return nullptr;
}

} // namespace rv_pdktools
