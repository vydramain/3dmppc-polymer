// A headless ImGui context for tests: no backend, a built font atlas and a fixed display, so widgets can lay out
// and be measured inside a frame.

#pragma once

#include "font/rv_editor_font.hpp"
#include "imgui.h"

namespace rv_editor_test
{

struct rv_editor_test_imgui {
    // `with_fonts`: the editor's own fonts, which panes drawing in the code font need. The font setup keeps its fonts in
    // globals, so they are added once, to one atlas that every such context shares (a second add would remove the
    // fonts of an atlas that is gone).
    explicit rv_editor_test_imgui(bool with_fonts = false)
    {
        ImGui::CreateContext(with_fonts ? &shared_font_atlas() : nullptr);
        ImGuiIO &io = ImGui::GetIO();
        io.DisplaySize = ImVec2(2000, 2000); // larger than any test window, so nothing is clipped
        io.IniFilename = nullptr;
        ImGui::GetStyle().WindowMinSize = ImVec2(1, 1); // a test window may be as low as a pane's minimum, under the default 32
        if (!with_fonts) {
            io.Fonts->AddFontDefault();
        }
        io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures; // 1.92: the atlas builds itself in NewFrame
    }
    static ImFontAtlas &shared_font_atlas()
    {
        static ImFontAtlas atlas;
        static const int added = rv_editor::rv_editor_fonts_add(atlas, 1.0f);
        (void)added;
        return atlas;
    }
    ~rv_editor_test_imgui()
    {
        ImGui::DestroyContext();
    }
    rv_editor_test_imgui(const rv_editor_test_imgui &) = delete;
    rv_editor_test_imgui &operator=(const rv_editor_test_imgui &) = delete;

    // One frame with a window at the origin, 400x300 unless a size is given; `body` draws inside it.
    template <class F>
    void frame(F body, float width = 400, float height = 300)
    {
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImVec2(width, height));
        ImGui::Begin("test", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings);
        body();
        ImGui::End();
        ImGui::EndFrame();
    }
};

} // namespace rv_editor_test
