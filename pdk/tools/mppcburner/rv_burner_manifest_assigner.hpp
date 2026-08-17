#pragma once

#include <functional>
#include <set>
#include <string>
#include <unordered_map>
#include <utility>

#include "rv_manifest.hpp"
#include "rv_burner_character.hpp"
#include "rv_burner_schema.hpp"

namespace rv_pdktools
{

// --- binding a value to a field -------------------------------------------

class rv_burner_manifest_assigner
{
private:
	rv_manifest manifest_{};
	std::set<std::string> seen_keys_{};

	// Поля, необходимые для привязки в конструкторе
	std::string &section_;
	std::string &error_;

	const std::unordered_map<
		std::string,
		std::function<
			bool(const rv_burner_mvalue &, rv_burner_manifest_assigner &)>>
		handlers_ = {
			{ std::string(rv_burner_section_disc) + "_" + std::string(rv_burner_key_disc_id),
				[](const rv_burner_mvalue &v, rv_burner_manifest_assigner &a) {
					if (v.kind != rv_burner_value_kind::string) {
						return a.wrong_type("id", v, rv_burner_value_kind::string);
					}
					a.manifest_.disc_id = v.str;
					return true;
				} },
			{ std::string(rv_burner_section_disc) + "_" + std::string(rv_burner_key_disc_title),
				[](const rv_burner_mvalue &v, rv_burner_manifest_assigner &a) {
					if (v.kind != rv_burner_value_kind::string) {
						return a.wrong_type("title", v, rv_burner_value_kind::string);
					}
					a.manifest_.disc_title = v.str;
					return true;
				} },
			{ std::string(rv_burner_section_build) + "_" + std::string(rv_burner_key_build_sources),
				[](const rv_burner_mvalue &v, rv_burner_manifest_assigner &a) {
					if (v.kind != rv_burner_value_kind::array) {
						return a.wrong_type("sources", v, rv_burner_value_kind::array);
					}
					a.manifest_.build_sources = v.arr;
					return true;
				} },
			{ std::string(rv_burner_section_build) + "_" + std::string(rv_burner_key_build_defines),
				[](const rv_burner_mvalue &v, rv_burner_manifest_assigner &a) {
					if (v.kind != rv_burner_value_kind::array) {
						return a.wrong_type("defines", v, rv_burner_value_kind::array);
					}
					a.manifest_.build_defines = v.arr;
					return true;
				} },
			{ std::string(rv_burner_section_build) + "_" + std::string(rv_burner_key_build_include_dirs),
				[](const rv_burner_mvalue &v, rv_burner_manifest_assigner &a) {
					if (v.kind != rv_burner_value_kind::array) {
						return a.wrong_type("include_dirs", v, rv_burner_value_kind::array);
					}
					a.manifest_.build_include_dirs = v.arr;
					return true;
				} },
			{ std::string(rv_burner_section_scripts) + "_" + std::string(rv_burner_key_scripts_sources),
				[](const rv_burner_mvalue &v, rv_burner_manifest_assigner &a) {
					if (v.kind != rv_burner_value_kind::array) {
						return a.wrong_type("sources", v, rv_burner_value_kind::array);
					}
					a.manifest_.scripts_sources = v.arr;
					return true;
				} },
			{ std::string(rv_burner_section_assets) + "_" + std::string(rv_burner_key_assets_files),
				[](const rv_burner_mvalue &v, rv_burner_manifest_assigner &a) {
					if (v.kind != rv_burner_value_kind::array) {
						return a.wrong_type("files", v, rv_burner_value_kind::array);
					}
					a.manifest_.assets_files = v.arr;
					return true;
				} },
			{ std::string(rv_burner_section_textures) + "_" + std::string(rv_burner_key_textures_files),
				[](const rv_burner_mvalue &v, rv_burner_manifest_assigner &a) {
					if (v.kind != rv_burner_value_kind::array) {
						return a.wrong_type("files", v, rv_burner_value_kind::array);
					}
					a.manifest_.textures_files.files = v.arr;
					return true;
				} },
			{ std::string(rv_burner_section_textures) + "_" + std::string(rv_burner_key_textures_format),
				[](const rv_burner_mvalue &v, rv_burner_manifest_assigner &a) {
					if (v.kind != rv_burner_value_kind::string) {
						return a.wrong_type("format", v, rv_burner_value_kind::string);
					}
					a.manifest_.textures_files.format = v.str;
					return true;
				} },
			{ std::string(rv_burner_section_budget) + "_" + std::string(rv_burner_key_budget_texture_max_width),
				[](const rv_burner_mvalue &v, rv_burner_manifest_assigner &a) {
					if (v.kind != rv_burner_value_kind::integer) {
						return a.wrong_type("texture_max_width", v, rv_burner_value_kind::integer);
					}
					a.manifest_.budget.texture_max_width = v.num;
					return true;
				} },
			{ std::string(rv_burner_section_budget) + "_" + std::string(rv_burner_key_budget_texture_max_height),
				[](const rv_burner_mvalue &v, rv_burner_manifest_assigner &a) {
					if (v.kind != rv_burner_value_kind::integer) {
						return a.wrong_type("texture_max_height", v, rv_burner_value_kind::integer);
					}
					a.manifest_.budget.texture_max_height = v.num;
					return true;
				} },
			{ std::string(rv_burner_section_budget) + "_" + std::string(rv_burner_key_budget_video_memory_size),
				[](const rv_burner_mvalue &v, rv_burner_manifest_assigner &a) {
					if (v.kind != rv_burner_value_kind::integer) {
						return a.wrong_type("video_memory_size", v, rv_burner_value_kind::integer);
					}
					a.manifest_.budget.video_memory_size = v.num;
					return true;
				} },
		};

public:
	rv_burner_manifest_assigner(std::string &section, std::string &error)
		: section_(section)
		, error_(error)
	{
	}

	bool wrong_type(const std::string &key, const rv_burner_mvalue &v, rv_burner_value_kind want);
	bool assign(const std::string &key, const rv_burner_mvalue &value, int key_line);

	// Hands the filled manifest out. The assigner is single-use, so the product
	// is moved rather than copied — after this the assigner holds an empty one.
	rv_manifest take_manifest()
	{
		return std::move(manifest_);
	}
};

} // namespace rv_pdktools
