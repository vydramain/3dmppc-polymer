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

constexpr std::string_view rv_burner_disc_keys[] = { "id", "title" };
constexpr std::string_view rv_burner_build_keys[] = { "sources", "defines", "include_dirs" };
constexpr std::string_view rv_burner_scripts_keys[] = { "sources" };
constexpr std::string_view rv_burner_assert_keys[] = { "files" };
constexpr std::string_view rv_burner_textures_keys[] = { "files", "format" };
constexpr std::string_view rv_burner_budget_keys[] = { "texture_max_width", "texture_max_height",
	"video_memory_size" };

struct section_spec {
	std::string_view name;
	const std::string_view *keys;
	std::size_t key_count;
};

constexpr section_spec rv_burner_sections[] = {
	{ "disc", rv_burner_disc_keys, std::size(rv_burner_disc_keys) },
	{ "build", rv_burner_build_keys, std::size(rv_burner_build_keys) },
	{ "scripts", rv_burner_scripts_keys, std::size(rv_burner_scripts_keys) },
	{ "assets", rv_burner_assert_keys, std::size(rv_burner_assert_keys) },
	{ "textures", rv_burner_textures_keys, std::size(rv_burner_textures_keys) },
	{ "budget", rv_burner_budget_keys, std::size(rv_burner_budget_keys) },
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
