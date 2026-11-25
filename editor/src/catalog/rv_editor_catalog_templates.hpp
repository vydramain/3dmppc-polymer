#pragma once

// The Widget Catalog's template sections: what each template section is made of, and the helpers its parts share.

#include <iterator>
#include <string>

#include "imgui.h"

#include "theme/rv_editor_theme.hpp"

namespace rv_editor
{

// One template section: its text keys, its states and the three things the catalog asks of it. Sizes are window sizes,
// the window padding included; draw fills the content region of the current window.
struct rv_editor_catalog_template {
    const char *name;
    const char *const *states;
    int state_count;
    ImVec2 (*tile_min)(const rv_editor_theme &theme);
    ImVec2 (*tile_roomy)(const rv_editor_theme &theme);
    void (*draw)(int state, const rv_editor_theme &theme);
};

extern const rv_editor_catalog_template rv_editor_catalog_template_tabs;
extern const rv_editor_catalog_template rv_editor_catalog_template_shelf;
extern const rv_editor_catalog_template rv_editor_catalog_template_list;
extern const rv_editor_catalog_template rv_editor_catalog_template_records;
extern const rv_editor_catalog_template rv_editor_catalog_template_message;
extern const rv_editor_catalog_template rv_editor_catalog_template_mode;
extern const rv_editor_catalog_template rv_editor_catalog_template_form;
extern const rv_editor_catalog_template rv_editor_catalog_template_transport;
extern const rv_editor_catalog_template rv_editor_catalog_template_picture;
extern const rv_editor_catalog_template rv_editor_catalog_template_facts;
extern const rv_editor_catalog_template rv_editor_catalog_template_status;
extern const rv_editor_catalog_template rv_editor_catalog_template_sections;

// The text of `key` in [catalog_templates].
const char *rv_editor_catalog_tpl_text(const char *key);

// The window size whose content region is `content`, and the padding on both axes.
ImVec2 rv_editor_catalog_tpl_window(ImVec2 content);

// The theme's padding in pixels at its scale.
inline float theme_pad(const rv_editor_theme &theme)
{
    return static_cast<float>(theme.pad_px * theme.scale);
}

// A roomy tile for a template whose minimum is `min`: clearly larger on both axes.
ImVec2 rv_editor_catalog_tpl_roomy(ImVec2 min);

// Defines rv_editor_catalog_template_<name> from the file's <name>_states, <name>_min and <name>_draw; the roomy tile is
// derived from the minimum.
#define RV_EDITOR_TPL(name)                                                                  \
    const rv_editor_catalog_template rv_editor_catalog_template_##name = { "section_" #name, \
        name##_states,                                                                       \
        static_cast<int>(std::size(name##_states)),                                          \
        name##_min,                                                                          \
        [](const rv_editor_theme &theme) {                                                   \
            return rv_editor_catalog_tpl_roomy(name##_min(theme));                           \
        },                                                                                   \
        name##_draw }

} // namespace rv_editor
