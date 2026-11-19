// A forward of ImGui's test-engine item hooks to an optional sink. No sink by default and ImGui calls the hooks only
// while the context's TestEngineHookItems is true, so production behaviour is unchanged.

#pragma once

#include "imgui.h"

namespace rv_editor
{

struct rv_editor_item_sink {
    virtual ~rv_editor_item_sink() = default;
    virtual void item_add(ImGuiID id, ImVec2 min, ImVec2 max) = 0; // every submitted item and window
    virtual void item_info(ImGuiID id, const char *label) = 0;     // the label of an item that has one
};

// nullptr removes the sink.
void rv_editor_item_sink_set(rv_editor_item_sink *sink);

} // namespace rv_editor
