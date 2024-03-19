// dear imgui test engine
// (ui)
// If you run tests in an interactive or visible application, you may want to call ImGuiTestEngine_ShowTestEngineWindows()

// Provide access to:
// - "Dear ImGui Test Engine" main interface
// - "Dear ImGui Capture Tool"
// - "Dear ImGui Perf Tool"
// - other core debug functions: Metrics, Debug Log

#pragma once

#ifndef IMGUI_VERSION
#include "../imgui.h"      // IMGUI_API
#endif

// Forward declarations
struct ImGuiTestEngine;

// Functions
IMGUI_API void    ImGuiTestEngine_ShowTestEngineWindows(ImGuiTestEngine* engine, bool* p_open);
