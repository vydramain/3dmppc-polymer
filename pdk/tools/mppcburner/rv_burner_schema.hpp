#pragma once

#include <cstddef>
#include <cstdlib>
#include <string>
#include <string_view>
#include <vector>

// --- the schema ---------------------------------------------------------------
//
// The one place the accepted spelling of every section and key lives. The
// parser rejects anything absent from these tables AND uses them to spell the
// "did you mean" hint, so a key added to rv_manifest must be added here or it
// stops being accepted — which is the failure mode we want, not the reverse.

// Every section and key is named once here. Tables elsewhere refer to these
// constants rather than to positions in the arrays below, so reordering an
// array cannot silently rebind anything.

constexpr std::string_view rv_burner_section_disc = "disc";
constexpr std::string_view rv_burner_section_build = "build";
constexpr std::string_view rv_burner_section_scripts = "scripts";
constexpr std::string_view rv_burner_section_assets = "assets";
constexpr std::string_view rv_burner_section_textures = "textures";
constexpr std::string_view rv_burner_section_budget = "budget";

// Section ['disk']
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

// Section ['disk']
constexpr std::string_view rv_burner_disc_keys[] = {
	rv_burner_key_disc_id,
	rv_burner_key_disc_title
};

constexpr std::string_view rv_burner_build_keys[] = {
	rv_burner_key_build_sources,
	rv_burner_key_build_defines,
	rv_burner_key_build_include_dirs
};

constexpr std::string_view rv_burner_scripts_keys[] = {
	rv_burner_key_scripts_sources
};

constexpr std::string_view rv_burner_assert_keys[] = {
	rv_burner_key_assets_files
};

constexpr std::string_view rv_burner_textures_keys[] = {
	rv_burner_key_textures_files,
	rv_burner_key_textures_format
};

constexpr std::string_view rv_burner_budget_keys[] = {
	rv_burner_key_budget_texture_max_width,
	rv_burner_key_budget_texture_max_height,
	rv_burner_key_budget_video_memory_size
};

struct section_spec {
	std::string_view name;
	const std::string_view *keys;
	std::size_t key_count;
};

constexpr section_spec rv_burner_sections[] = {
	{ rv_burner_section_disc, rv_burner_disc_keys, std::size(rv_burner_disc_keys) },
	{ rv_burner_section_build, rv_burner_build_keys, std::size(rv_burner_build_keys) },
	{ rv_burner_section_scripts, rv_burner_scripts_keys, std::size(rv_burner_scripts_keys) },
	{ rv_burner_section_assets, rv_burner_assert_keys, std::size(rv_burner_assert_keys) },
	{ rv_burner_section_textures, rv_burner_textures_keys, std::size(rv_burner_textures_keys) },
	{ rv_burner_section_budget, rv_burner_budget_keys, std::size(rv_burner_budget_keys) },
};

// The spellings rv_manifest_validate accepts for [textures] format; they match
// the rv_texfmt enumerators of pdk/cv/rv_texture.hpp one for one.
constexpr std::string_view rv_burner_approved_texture_formats[] = { "idx4", "idx8", "direct15" };

const inline section_spec *rv_burner_sections_get(std::string_view name)
{
	for (const section_spec &s : rv_burner_sections) {
		if (s.name == name) {
			return &s;
		}
	}
	return nullptr;
}
