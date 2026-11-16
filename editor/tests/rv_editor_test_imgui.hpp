// A headless ImGui context for tests: no backend, a built font atlas and a fixed display, so widgets can lay out
// and be measured inside a frame.

#pragma once

#include "imgui.h"

namespace rv_editor_test
{

struct rv_editor_test_imgui {
    rv_editor_test_imgui()
    {
        ImGui::CreateContext();
        ImGuiIO &io = ImGui::GetIO();
        io.DisplaySize = ImVec2(800, 600);
        io.IniFilename = nullptr;
        io.Fonts->AddFontDefault();
        io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures; // 1.92: the atlas builds itself in NewFrame
    }
    ~rv_editor_test_imgui()
    {
        ImGui::DestroyContext();
    }
    rv_editor_test_imgui(const rv_editor_test_imgui &) = delete;
    rv_editor_test_imgui &operator=(const rv_editor_test_imgui &) = delete;

    // One frame with a window at the origin whose content is 400 px wide; `body` draws inside it.
    template <class F>
    void frame(F body)
    {
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImVec2(400, 300));
        ImGui::Begin("test", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings);
        body();
        ImGui::End();
        ImGui::EndFrame();
    }
};

} // namespace rv_editor_test
