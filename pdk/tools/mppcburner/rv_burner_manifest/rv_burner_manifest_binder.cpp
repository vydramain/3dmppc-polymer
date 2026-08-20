#include "rv_burner_manifest_binder.hpp"

#include <string_view>

#include "pdklib/rv_texfmt_name.hpp"

#include "rv_burner_manifest_value.hpp"
#include "rv_burner_schema.hpp"

namespace rv_pdktools
{
namespace
{

// One row per field of rv_burner_manifest. Section and key come from the schema
// constants, so a row cannot drift away from the spelling the parser accepts.
struct bind_rule {
	std::string_view section;
	std::string_view key;
	void (*apply)(rv_burner_manifest &, const rv_burner_mvalue &);
};

constexpr bind_rule RULES[] = {
	{ rv_burner_section_disc, // [disc] id = "…"
		rv_burner_key_disc_id,
		[](rv_burner_manifest &m, const rv_burner_mvalue &v) {
			m.disc_id = v.str;
		} },
	{ rv_burner_section_disc, // [disc] title = "…"
		rv_burner_key_disc_title,
		[](rv_burner_manifest &m, const rv_burner_mvalue &v) {
			m.disc_title = v.str;
		} },

	{
		// [build] sources = [ … ]
		rv_burner_section_build,
		rv_burner_key_build_sources,
		[](rv_burner_manifest &m, const rv_burner_mvalue &v) {
			m.build_sources = v.arr;
		},
	},
	{ rv_burner_section_build, // [build] defines = [ … ]
		rv_burner_key_build_defines,
		[](rv_burner_manifest &m, const rv_burner_mvalue &v) {
			m.build_defines = v.arr;
		} },
	{ rv_burner_section_build, // [build] include_dirs = [ … ]
		rv_burner_key_build_include_dirs,
		[](rv_burner_manifest &m, const rv_burner_mvalue &v) {
			m.build_include_dirs = v.arr;
		} },

	{ rv_burner_section_scripts, // [scripts] sources = [ … ]
		rv_burner_key_scripts_sources,
		[](rv_burner_manifest &m, const rv_burner_mvalue &v) {
			m.scripts_sources = v.arr;
		} },

	{ rv_burner_section_assets, // [assets] files = [ … ]
		rv_burner_key_assets_files,
		[](rv_burner_manifest &m, const rv_burner_mvalue &v) {
			m.assets_files = v.arr;
		} },

	{ rv_burner_section_textures, // [textures] files = [ … ]
		rv_burner_key_textures_files,
		[](rv_burner_manifest &m, const rv_burner_mvalue &v) {
			m.textures_files.files = v.arr;
		} },
	{ rv_burner_section_textures, // [textures] format = "…"
		rv_burner_key_textures_format,
		[](rv_burner_manifest &m, const rv_burner_mvalue &v) {
			const rv_pdklib::rv_texfmt_name *row = rv_pdklib::rv_texfmt_name::by_text(v.str.c_str());
			if (row != nullptr) {
				m.textures_files.format = row->format;
			}
		} },

	{ rv_burner_section_budget, // [budget] texture_max_width = N
		rv_burner_key_budget_texture_max_width,
		[](rv_burner_manifest &m, const rv_burner_mvalue &v) {
			m.budget.texture_max_width = v.num;
		} },
	{ rv_burner_section_budget, // [budget] texture_max_height = N
		rv_burner_key_budget_texture_max_height,
		[](rv_burner_manifest &m, const rv_burner_mvalue &v) {
			m.budget.texture_max_height = v.num;
		} },
	{ rv_burner_section_budget, // [budget] video_memory_size = N
		rv_burner_key_budget_video_memory_size,
		[](rv_burner_manifest &m, const rv_burner_mvalue &v) {
			m.budget.video_memory_size = v.num;
		} },
};

const bind_rule *find_rule(std::string_view section, std::string_view key)
{
	for (const bind_rule &rule : RULES) {
		if (rule.section == section && rule.key == key) {
			return &rule;
		}
	}
	return nullptr;
}

} // namespace

rv_burner_manifest rv_burner_manifest_bind(const rv_burner_tree &tree)
{
	// Whatever the manifest leaves unsaid keeps the default of the structure —
	// the defaults live in rv_burner_manifest.hpp and nowhere else.
	rv_burner_manifest manifest{};

	for (const rv_burner_tree_section &section : tree.sections) {
		for (const rv_burner_tree_entry &entry : section.entries) {
			const bind_rule *rule = find_rule(section.name, entry.key);
			if (rule != nullptr) {
				rule->apply(manifest, entry.value);
			}
		}
	}
	return manifest;
}

} // namespace rv_pdktools
