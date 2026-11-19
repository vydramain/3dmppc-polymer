// The functions imgui.cpp calls when IMGUI_ENABLE_TEST_ENGINE is defined, forwarding to the sink.

#include "ui/rv_editor_item_hook.hpp"

#include "imgui_internal.h"

namespace rv_editor
{

namespace
{
rv_editor_item_sink *sink_in_use = nullptr;
}

void rv_editor_item_sink_set(rv_editor_item_sink *sink)
{
    sink_in_use = sink;
}

} // namespace rv_editor

void ImGuiTestEngineHook_ItemAdd(ImGuiContext *, ImGuiID id, const ImRect &bb, const ImGuiLastItemData *)
{
    if (rv_editor::sink_in_use != nullptr) {
        rv_editor::sink_in_use->item_add(id, bb.Min, bb.Max);
    }
}

void ImGuiTestEngineHook_ItemInfo(ImGuiContext *, ImGuiID id, const char *label, ImGuiItemStatusFlags)
{
    if (rv_editor::sink_in_use != nullptr) {
        rv_editor::sink_in_use->item_info(id, label);
    }
}

void ImGuiTestEngineHook_Log(ImGuiContext *, const char *, ...)
{
}

const char *ImGuiTestEngine_FindItemDebugLabel(ImGuiContext *, ImGuiID)
{
    return nullptr;
}
