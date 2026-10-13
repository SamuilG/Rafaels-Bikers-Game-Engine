#pragma once

#include <imgui.h>
#include <imgui_internal.h>

namespace engine::editor_viewport {

// GLFW's disabled cursor still reports unbounded virtual coordinates. Gameplay
// events must never activate editor widgets, including after capture is released.
inline void SetCapturedInput(bool captured) {
    ImGuiIO& io = ImGui::GetIO();
    constexpr ImGuiConfigFlags flags = ImGuiConfigFlags_NoMouse | ImGuiConfigFlags_NoKeyboard;
    const bool wasCaptured = (io.ConfigFlags & ImGuiConfigFlags_NoMouse) != 0;
    if (captured) io.ConfigFlags |= flags;
    else io.ConfigFlags &= ~flags;
    if (captured || wasCaptured) {
        io.ClearInputMouse();
        io.ClearInputKeys();
        auto& events = ImGui::GetCurrentContext()->InputEventsQueue;
        for (int index = 0; index < events.Size;) {
            const ImGuiInputEventType type = events[index].Type;
            if (type == ImGuiInputEventType_MousePos || type == ImGuiInputEventType_MouseButton ||
                type == ImGuiInputEventType_MouseWheel || type == ImGuiInputEventType_MouseViewport ||
                type == ImGuiInputEventType_Key || type == ImGuiInputEventType_Text)
                events.erase(events.Data + index);
            else ++index; // Preserve OS focus events, including Alt+Tab.
        }
    }
    if (captured != wasCaptured) ImGui::ClearActiveID();
}

} // namespace engine::editor_viewport
