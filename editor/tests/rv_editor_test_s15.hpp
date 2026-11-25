// Helpers of the S15 tests: the fixture and finders, and what the message box drew, a state drawn and compared.

#pragma once

#include "rv_editor_test_s15_find.hpp"
#include "rv_editor_test_s15_fixture.hpp"

namespace rv_editor_test
{

namespace s15
{

// The count of glyphs of `chars` the pane draws in one frame at width x height, after settling frames.
inline std::vector<int> glyph_counts(fixture &f, float width, float height, const std::string &chars)
{
    f.settled(width, height);
    std::vector<ImVec2> uv(chars.size());
    f.gui.frame(
        [&] {
            rv_editor_shell_pane(&f.shell, pane, f.kind, stable_tiles::theme);
            for (size_t i = 0; i < chars.size(); ++i) {
                const ImFontGlyph *g = ImGui::GetFontBaked()->FindGlyph(static_cast<ImWchar>(chars[i]));
                uv[i] = ImVec2(g->U0, g->V0);
            }
        },
        width,
        height);
    std::vector<int> counts(chars.size(), 0);
    for (ImGuiWindow *window : ImGui::GetCurrentContext()->Windows) {
        for (const ImDrawVert &v : window->DrawList->VtxBuffer) {
            for (size_t i = 0; i < chars.size(); ++i) {
                if (std::abs(v.uv.x - uv[i].x) < 1e-6f && std::abs(v.uv.y - uv[i].y) < 1e-6f) {
                    ++counts[i];
                }
            }
        }
    }
    return counts;
}

// What the message box's text window drew in the frame just ended: the draw commands of the window and its children, so
// a box with a text draws more than an empty one.
inline int box_drawn(const rv_editor_probe_record &r)
{
    const rv_editor_probe_entry *box = message_box(r);
    const ImGuiWindow *w = box != nullptr ? ImGui::FindWindowByName(box->label.c_str()) : nullptr;
    int n = 0;
    for (const ImGuiWindow *x : ImGui::GetCurrentContext()->Windows) {
        for (const ImGuiWindow *p = x; w != nullptr && p != nullptr; p = p->ParentWindow) {
            if (p == w) {
                for (const ImDrawCmd &cmd : x->DrawList->CmdBuffer) {
                    n += static_cast<int>(cmd.ElemCount);
                }
                break;
            }
        }
    }
    return n;
}

// The state `index` of `f` drawn at `width` x `height`, with what the box drew (valid until the next frame).
struct shot : rv_editor_probe_record {
    int drawn = 0;
};

inline shot snap(fixture &f, size_t index, float width, float height)
{
    shot s;
    f.states[index]();
    static_cast<rv_editor_probe_record &>(s) = f.settled(width, height);
    s.drawn = box_drawn(s);
    return s;
}

// "" when both draws have every labelled item of `anchors` and give every item they share the same rectangle.
inline std::string
same_items(const rv_editor_probe_record &a, const rv_editor_probe_record &b, const std::vector<std::string> &anchors)
{
    using namespace rv_editor_probe_detail;
    for (const std::string &anchor : anchors) {
        if (find_text(a, anchor) == nullptr && find_id(a, anchor) == nullptr) {
            return "'" + anchor + "' is missing from the first draw";
        }
        if (find_text(b, anchor) == nullptr && find_id(b, anchor) == nullptr) {
            return "'" + anchor + "' is missing from the second draw";
        }
    }
    for (const auto &ea : a.entries) {
        if (ea.id == 0 || ea.label.empty()) {
            continue;
        }
        for (const auto &eb : b.entries) {
            if (eb.id == ea.id && (ea.x0 != eb.x0 || ea.y0 != eb.y0 || ea.x1 != eb.x1 || ea.y1 != eb.y1)) {
                return name(ea) + " " + rect(ea) + " became " + rect(eb);
            }
        }
    }
    return "";
}

} // namespace s15

} // namespace rv_editor_test
