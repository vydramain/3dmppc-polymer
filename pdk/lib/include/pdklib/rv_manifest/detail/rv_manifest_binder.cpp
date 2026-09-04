#include "rv_manifest_binder.hpp"

#include <cstdint>
#include <string_view>

#include "pdklib/rv_textures/rv_texfmt_name.hpp"

#include "rv_manifest_value.hpp"
#include "rv_manifest_schema.hpp"

namespace rv_pdklib
{
// One row per field of rv_manifest. Section and key come from the schema
// constants, so a row cannot drift away from the spelling the parser accepts.
struct bind_rule {
    std::string_view section;
    std::string_view key;
    void (*apply)(rv_manifest &, const rv_manifest_mvalue &);
};

constexpr bind_rule RULES[] = {
    { rv_manifest_section_disc, // [disc] id = "…"
        rv_manifest_key_disc_id,
        [](rv_manifest &m, const rv_manifest_mvalue &v) {
            m.disc_id = v.str;
        } },
    { rv_manifest_section_disc, // [disc] title = "…"
        rv_manifest_key_disc_title,
        [](rv_manifest &m, const rv_manifest_mvalue &v) {
            m.disc_title = v.str;
        } },
    {
        rv_manifest_section_build, // [build] sources = [ … ]
        rv_manifest_key_build_sources,
        [](rv_manifest &m, const rv_manifest_mvalue &v) {
            m.build_sources = v.arr;
        },
    },
    { rv_manifest_section_build, // [build] defines = [ … ]
        rv_manifest_key_build_defines,
        [](rv_manifest &m, const rv_manifest_mvalue &v) {
            m.build_defines = v.arr;
        } },
    { rv_manifest_section_build, // [build] include_dirs = [ … ]
        rv_manifest_key_build_include_dirs,
        [](rv_manifest &m, const rv_manifest_mvalue &v) {
            m.build_include_dirs = v.arr;
        } },

    { rv_manifest_section_scripts, // [scripts] sources = [ … ]
        rv_manifest_key_scripts_sources,
        [](rv_manifest &m, const rv_manifest_mvalue &v) {
            m.scripts_sources = v.arr;
        } },

    { rv_manifest_section_assets, // [assets] files = [ … ]
        rv_manifest_key_assets_files,
        [](rv_manifest &m, const rv_manifest_mvalue &v) {
            m.assets_files = v.arr;
        } },
    { rv_manifest_section_textures, // [textures] files = [ … ]
        rv_manifest_key_textures_files,
        [](rv_manifest &m, const rv_manifest_mvalue &v) {
            m.textures_files.files = v.arr;
        } },
    { rv_manifest_section_textures, // [textures] format = "…"
        rv_manifest_key_textures_format,
        [](rv_manifest &m, const rv_manifest_mvalue &v) {
            const rv_pdklib::rv_texfmt_name *row = rv_pdklib::rv_texfmt_name::by_text(v.str.c_str());
            if (row != nullptr) {
                m.textures_files.format = row->format;
            }
        } },
    { rv_manifest_section_budget, // [budget] headless = true|false
        rv_manifest_key_budget_headless,
        [](rv_manifest &m, const rv_manifest_mvalue &v) {
            m.budget.headless = static_cast<bool>(v.num);
        } },
    { rv_manifest_section_budget, // [budget] fixed_step = true|false
        rv_manifest_key_budget_fixed_step,
        [](rv_manifest &m, const rv_manifest_mvalue &v) {
            m.budget.fixed_step = static_cast<bool>(v.num);
        } },
    { rv_manifest_section_budget, // [budget] scale = N
        rv_manifest_key_budget_scale,
        [](rv_manifest &m, const rv_manifest_mvalue &v) {
            m.budget.scale = static_cast<uint64_t>(v.num);
        } },
    { rv_manifest_section_budget, // [budget] max_frames = N
        rv_manifest_key_budget_max_frames,
        [](rv_manifest &m, const rv_manifest_mvalue &v) {
            m.budget.max_frames = static_cast<uint64_t>(v.num);
        } },
    { rv_manifest_section_budget_pcca, // [budget.pcca] voice_count = N
        rv_manifest_key_budget_pcca_voice_count,
        [](rv_manifest &m, const rv_manifest_mvalue &v) {
            m.budget.pcca.voice_count = v.num;
        } },
    { rv_manifest_section_budget_pcca, // [budget.pcca] sound_memory_size = N
        rv_manifest_key_budget_pcca_sound_memory_size,
        [](rv_manifest &m, const rv_manifest_mvalue &v) {
            m.budget.pcca.sound_memory_size = v.num;
        } },
    { rv_manifest_section_budget_pccv, // [budget.pccv] screen_width = N
        rv_manifest_key_budget_pccv_screen_width,
        [](rv_manifest &m, const rv_manifest_mvalue &v) {
            m.budget.pccv.screen_width = v.num;
        } },
    { rv_manifest_section_budget_pccv, // [budget.pccv] screen_height = N
        rv_manifest_key_budget_pccv_screen_height,
        [](rv_manifest &m, const rv_manifest_mvalue &v) {
            m.budget.pccv.screen_height = v.num;
        } },
    { rv_manifest_section_budget_pccv, // [budget.pccv] texture_max_width = N
        rv_manifest_key_budget_pccv_texture_max_width,
        [](rv_manifest &m, const rv_manifest_mvalue &v) {
            m.budget.pccv.texture_max_width = v.num;
        } },
    { rv_manifest_section_budget_pccv, // [budget.pccv] texture_max_height = N
        rv_manifest_key_budget_pccv_texture_max_height,
        [](rv_manifest &m, const rv_manifest_mvalue &v) {
            m.budget.pccv.texture_max_height = v.num;
        } },
    { rv_manifest_section_budget_pccv, // [budget.pccv] video_memory_size = N
        rv_manifest_key_budget_pccv_video_memory_size,
        [](rv_manifest &m, const rv_manifest_mvalue &v) {
            m.budget.pccv.video_memory_size = v.num;
        } },
    { rv_manifest_section_budget_pccv, // [budget.pccv] frame_capacity = N
        rv_manifest_key_budget_pccv_frame_capacity,
        [](rv_manifest &m, const rv_manifest_mvalue &v) {
            m.budget.pccv.frame_capacity = v.num;
        } },
    { rv_manifest_section_budget_pccv, // [budget.pccv] ot_bucket_count = N
        rv_manifest_key_budget_pccv_ot_bucket_count,
        [](rv_manifest &m, const rv_manifest_mvalue &v) {
            m.budget.pccv.ot_bucket_count = v.num;
        } },
    { rv_manifest_section_budget_pccio, // [budget.pccio] iport_count = N
        rv_manifest_key_budget_pccio_iport_count,
        [](rv_manifest &m, const rv_manifest_mvalue &v) {
            m.budget.pccio.iport_count = v.num;
        } },
    { rv_manifest_section_budget_pccm, // [budget.pccm] card_slots = N
        rv_manifest_key_budget_pccm_card_slots,
        [](rv_manifest &m, const rv_manifest_mvalue &v) {
            m.budget.pccm.card_slots = v.num;
        } },
    { rv_manifest_section_budget_pccm, // [budget.pccm] card_slot_size = N
        rv_manifest_key_budget_pccm_card_slot_size,
        [](rv_manifest &m, const rv_manifest_mvalue &v) {
            m.budget.pccm.card_slot_size = v.num;
        } },
    { rv_manifest_section_budget_pccd, // [budget.pccd] medium_path = "…"
        rv_manifest_key_budget_pccd_medium_path,
        [](rv_manifest &m, const rv_manifest_mvalue &v) {
            m.budget.pccd.medium_path = v.str;
        } },
};

static const bind_rule *find_rule(std::string_view section, std::string_view key)
{
    for (const bind_rule &rule : RULES) {
        if (rule.section == section && rule.key == key) {
            return &rule;
        }
    }
    return nullptr;
}
rv_manifest rv_manifest_bind(const rv_manifest_tree &tree)
{
    // Whatever the manifest leaves unsaid keeps the default of the structure —
    // the defaults live in rv_manifest.hpp and nowhere else.
    rv_manifest manifest{};

    for (const rv_manifest_tree_section &section : tree.sections) {
        for (const rv_manifest_tree_entry &entry : section.entries) {
            const bind_rule *rule = find_rule(section.name, entry.key);
            if (rule != nullptr) {
                rule->apply(manifest, entry.value);
            }
        }
    }
    return manifest;
}

} // namespace rv_pdklib
