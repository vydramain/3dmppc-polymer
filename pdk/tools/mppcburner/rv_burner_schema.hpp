#pragma once

#include <cctype>
#include <cerrno>
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

constexpr std::string_view rv_burn_disc_keys[] = { "id", "title" };
constexpr std::string_view rv_burn_build_keys[] = { "sources", "defines", "include_dirs" };
constexpr std::string_view rv_burn_scripts_keys[] = { "sources" };
constexpr std::string_view rv_burn_assert_keys[] = { "files" };
constexpr std::string_view rv_burn_textures_keys[] = { "files", "format" };
constexpr std::string_view rv_burn_budget_keys[] = { "texture_max_width", "texture_max_height",
	"video_memory_size" };

struct section_spec {
	std::string_view name;
	const std::string_view *keys;
	std::size_t key_count;
};

constexpr section_spec rv_burn_sections[] = {
	{ "disc", rv_burn_disc_keys, std::size(rv_burn_disc_keys) },
	{ "build", rv_burn_build_keys, std::size(rv_burn_build_keys) },
	{ "scripts", rv_burn_scripts_keys, std::size(rv_burn_scripts_keys) },
	{ "assets", rv_burn_assert_keys, std::size(rv_burn_assert_keys) },
	{ "textures", rv_burn_textures_keys, std::size(rv_burn_textures_keys) },
	{ "budget", rv_burn_budget_keys, std::size(rv_burn_budget_keys) },
};

// The spellings rv_manifest_validate accepts for [textures] format; they match
// the rv_texfmt enumerators of pdk/cv/rv_texture.hpp one for one.
constexpr std::string_view rv_burn_approved_texture_formats[] = { "idx4", "idx8", "direct15" };
