#pragma once

// Dear ImGui build options for the editor, picked up through IMGUI_USER_CONFIG
// so the vendored imconfig.h stays untouched.

#define IMGUI_DISABLE_OBSOLETE_FUNCTIONS

// imgui_demo.cpp is not vendored.
#define IMGUI_DISABLE_DEMO_WINDOWS

// ImGui's item hooks (see ui/rv_editor_item_hook.hpp): off at run time until a sink is set, so the editor behaves as
// before; the layout probe of the tests records every submitted item through them.
#define IMGUI_ENABLE_TEST_ENGINE
