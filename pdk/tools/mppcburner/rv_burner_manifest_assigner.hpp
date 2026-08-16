#pragma once

#include <functional>
#include <set>
#include <string>
#include <unordered_map>

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

	const std::unordered_map<std::string, std::function<bool(const rv_burner_mvalue &)>> handlers_ = {
		{ std::string(rv_burner_sections[0].name) + "_" + std::string(rv_burner_disc_keys[0]), // [disc] - id
			[this](const rv_burner_mvalue &v) {
				if (v.kind != rv_burner_value_kind::string) {
					return wrong_type("id", v, rv_burner_value_kind::string);
				}
				manifest_.disc_id = v.str;
				return true;
			} },
		{ std::string(rv_burner_sections[0].name) + "_" + std::string(rv_burner_disc_keys[1]), // [disc] - title
			[this](const rv_burner_mvalue &v) {
				if (v.kind != rv_burner_value_kind::string) {
					return wrong_type("title", v, rv_burner_value_kind::string);
				}
				manifest_.disc_title = v.str;
				return true;
			} },
		{ std::string(rv_burner_sections[1].name) + "_" + std::string(rv_burner_build_keys[0]), // [build] - sources
			[this](const rv_burner_mvalue &v) {
				if (v.kind != rv_burner_value_kind::array) {
					return wrong_type("sources", v, rv_burner_value_kind::array);
				}
				manifest_.build_sources = v.arr;
				return true;
			} },
		{ std::string(rv_burner_sections[1].name) + "_" + std::string(rv_burner_build_keys[1]), // [build] - defines
			[this](const rv_burner_mvalue &v) {
				if (v.kind != rv_burner_value_kind::array) {
					return wrong_type("defines", v, rv_burner_value_kind::array);
				}
				manifest_.build_defines = v.arr;
				return true;
			} },
		{ std::string(rv_burner_sections[1].name) + "_" + std::string(rv_burner_build_keys[2]), // [build] - include_dirs
			[this](const rv_burner_mvalue &v) {
				if (v.kind != rv_burner_value_kind::array) {
					return wrong_type("include_dirs", v, rv_burner_value_kind::array);
				}
				manifest_.build_include_dirs = v.arr;
				return true;
			} },
		{ std::string(rv_burner_sections[2].name) + "_" + std::string(rv_burner_scripts_keys[0]), // [scripts] - sources
			[this](const rv_burner_mvalue &v) {
				if (v.kind != rv_burner_value_kind::array) {
					return wrong_type("sources", v, rv_burner_value_kind::array);
				}
				manifest_.scripts_sources = v.arr;
				return true;
			} },
		{ std::string(rv_burner_sections[3].name) + "_" + std::string(rv_burner_assert_keys[0]), // [assets] - files
			[this](const rv_burner_mvalue &v) {
				if (v.kind != rv_burner_value_kind::array) {
					return wrong_type("files", v, rv_burner_value_kind::array);
				}
				manifest_.assets_files = v.arr;
				return true;
			} },
		{ std::string(rv_burner_sections[4].name) + "_" + std::string(rv_burner_textures_keys[0]), // [textures] - files
			[this](const rv_burner_mvalue &v) {
				if (v.kind != rv_burner_value_kind::array) {
					return wrong_type("files", v, rv_burner_value_kind::array);
				}
				manifest_.textures_files.files = v.arr;
				return true;
			} },
		{ std::string(rv_burner_sections[4].name) + "_" + std::string(rv_burner_textures_keys[1]), // [textures] - format
			[this](const rv_burner_mvalue &v) {
				if (v.kind != rv_burner_value_kind::string) {
					return wrong_type("format", v, rv_burner_value_kind::string);
				}
				manifest_.textures_files.format = v.str;
				return true;
			} },
		{ std::string(rv_burner_sections[5].name) + "_" + std::string(rv_burner_budget_keys[0]), // [budget] - texture_max_width
			[this](const rv_burner_mvalue &v) {
				if (v.kind != rv_burner_value_kind::integer) {
					return wrong_type("texture_max_width", v, rv_burner_value_kind::integer);
				}
				manifest_.budget.texture_max_width = v.num;
				return true;
			} },
		{ std::string(rv_burner_sections[5].name) + "_" + std::string(rv_burner_budget_keys[1]), // [budget] - texture_max_height
			[this](const rv_burner_mvalue &v) {
				if (v.kind != rv_burner_value_kind::integer) {
					return wrong_type("texture_max_height", v, rv_burner_value_kind::integer);
				}
				manifest_.budget.texture_max_height = v.num;
				return true;
			} },
		{ std::string(rv_burner_sections[5].name) + "_" + std::string(rv_burner_budget_keys[2]), // [budget] - video_memory_size
			[this](const rv_burner_mvalue &v) {
				if (v.kind != rv_burner_value_kind::integer) {
					return wrong_type("video_memory_size", v, rv_burner_value_kind::integer);
				}
				manifest_.budget.video_memory_size = v.num;
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
};

} // namespace rv_pdktools
