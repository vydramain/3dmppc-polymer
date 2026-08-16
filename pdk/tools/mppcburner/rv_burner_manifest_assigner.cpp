#include "rv_burner_manifest_assigner.hpp"

#include "rv_burner_manifest_failer.hpp"
#include "rv_burner_schema.hpp"
#include "rv_burner_text.hpp"

bool rv_pdktools::rv_burner_manifest_assigner::wrong_type(const std::string &key, const rv_burner_mvalue &v, rv_burner_value_kind want)
{
	return rv_burner_manifest_failer::fail(v.line, "key '" + key + "' in [" + section_ + "] takes " + std::string(rv_burner_kind_name(want)) + ", not " + std::string(rv_burner_kind_name(v.kind)), error_);
}

bool rv_pdktools::rv_burner_manifest_assigner::assign(const std::string &key, const rv_burner_mvalue &value, int key_line)
{
	const section_spec *spec = rv_burner_sections_get(section_);
	if (spec == nullptr) {
		return rv_burner_manifest_failer::fail(key_line, "internal error: unknown current section", error_);
	}

	bool known = false;
	for (std::size_t i = 0; i < spec->key_count; ++i) {
		if (spec->keys[i] == key) {
			known = true;
		}
	}
	if (!known) {
		return rv_burner_manifest_failer::fail(key_line, "unknown key '" + key + "' in [" + section_ + "]" + rv_pdktools::suggest(key, spec->keys, spec->key_count), error_);
	}
	if (!seen_keys_.insert(section_ + "." + key).second) {
		return rv_burner_manifest_failer::fail(key_line, "key '" + key + "' in [" + section_ + "] is set twice", error_);
	}

	if (section_ == "disc") {
		if (key == "id" || key == "title") {
			if (value.kind != rv_burner_value_kind::string) {
				return wrong_type(key, value, rv_burner_value_kind::string);
			}
			(key == "id" ? manifest_.disc_id : manifest_.disc_title) = value.str;
			return true;
		}
		if (value.kind != rv_burner_value_kind::integer) {
			return wrong_type(key, value, rv_burner_value_kind::integer);
		}
		return true;
	}

	if (section_ == "build" || section_ == "assets" ||
		(section_ == "textures" && key == "files")) {
		if (value.kind != rv_burner_value_kind::array) {
			return wrong_type(key, value, rv_burner_value_kind::array);
		}
		if (key == "sources") {
			manifest_.build_sources = value.arr;
		} else if (key == "defines") {
			manifest_.build_defines = value.arr;
		} else if (key == "include_dirs") {
			manifest_.build_include_dirs = value.arr;
		} else if (section_ == "assets") {
			manifest_.assets_files = value.arr;
		} else {
			manifest_.textures_files.files = value.arr;
		}
		return true;
	}

	if (section_ == "scripts") {
		if (key == "sources") {
			manifest_.scripts_sources = value.arr;
		}
		return true;
	}

	if (section_ == "textures") { // key == "format"
		if (value.kind != rv_burner_value_kind::string) {
			return wrong_type(key, value, rv_burner_value_kind::string);
		}
		manifest_.textures_files.format = value.str;
		return true;
	}

	// [budget]
	if (value.kind != rv_burner_value_kind::integer) {
		return wrong_type(key, value, rv_burner_value_kind::integer);
	}
	if (key == "texture_max_width") {
		manifest_.budget.texture_max_width = value.num;
	} else if (key == "texture_max_height") {
		manifest_.budget.texture_max_height = value.num;
	} else {
		manifest_.budget.video_memory_size = value.num;
	}
	return true;
}
