// Helpers of the content-area tests: what a content area drew (scrollbars, rectangles) and frames that settle.

#pragma once

#include <string>

#include "imgui_internal.h"
#include "rv_editor_test.hpp"
#include "rv_editor_test_imgui.hpp"
#include "rv_editor_test_layout_probe.hpp"
#include "text/rv_editor_text.hpp"
#include "theme/rv_editor_theme_imgui.hpp"
#include "ui/rv_editor_widgets.hpp"
#include "ui/rv_editor_widgets_content.hpp"

namespace rv_editor_test
{

namespace content
{

using namespace rv_editor;

inline constexpr rv_editor_theme theme = rv_editor_theme_olive;

inline void load_texts()
{
    std::string error;
    RV_EDITOR_CHECK(rv_editor_text_load({}, error) == 0);
}

// Which Motif scrollbars the frame drew: a track taller than wide is the vertical one.
struct bars {
    bool v = false;
    bool h = false;
};

inline bars bars_of(const rv_editor_probe_record &r)
{
    bars b;
    for (const auto &e : r.entries) {
        if (e.label == "##track") {
            ((e.y1 - e.y0) > (e.x1 - e.x0) ? b.v : b.h) = true;
        }
    }
    return b;
}

inline const rv_editor_probe_entry *area_of(const rv_editor_probe_record &r)
{
    for (const auto &e : r.entries) {
        if (e.scroll_area) {
            return &e;
        }
    }
    return nullptr;
}

// Two settling frames (a scroll area decides its bars and widths from the previous frame), then the recorded one.
template <class F>
rv_editor_probe_record settled(rv_editor_test_imgui &gui, float width, float height, F body)
{
    rv_editor_probe_draw(gui, width, body, height);
    rv_editor_probe_draw(gui, width, body, height);
    return rv_editor_probe_draw(gui, width, body, height);
}

inline ImRect last_item()
{
    return ImRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
}

inline std::string long_text(size_t chars)
{
    std::string s;
    while (s.size() < chars) {
        s += "segment" + std::to_string(s.size() % 10) + "/";
    }
    return s;
}

// True when a drawn label of the record shows "..." (the part before "##" is what is drawn).
inline bool shows_ellipsis(const rv_editor_probe_record &r)
{
    for (const auto &e : r.entries) {
        if (e.label.substr(0, e.label.find("##")).find("...") != std::string::npos) {
            return true;
        }
    }
    return false;
}

inline bool near(float a, float b, float eps = 0.5f)
{
    return a > b - eps && a < b + eps;
}

} // namespace content

} // namespace rv_editor_test
