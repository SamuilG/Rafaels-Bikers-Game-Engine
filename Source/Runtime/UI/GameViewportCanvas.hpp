#pragma once

#include <imgui.h>

namespace engine::editor_viewport {

struct CanvasInput {
    bool hovered = false;
    bool focused = false;
};

// Runtime widgets are drawn into this canvas but handle their own mouse events.
// Give their parent surface an ImGui input ID so the first press focuses Game
// before input forwarding, rather than at EndFrame after an empty Image click.
inline CanvasInput DrawGameCanvas(ImTextureID texture, const ImVec2& size) {
    const ImVec2 position = ImGui::GetCursorScreenPos();
    if (texture != ImTextureID{}) ImGui::Image(texture, size);
    else ImGui::Dummy(size);
    ImGui::SetCursorScreenPos(position);
    ImGui::InvisibleButton("##GameCanvasInput", size,
        ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);
    return {ImGui::IsItemHovered(), ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)};
}

} // namespace engine::editor_viewport
