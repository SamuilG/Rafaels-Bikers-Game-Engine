#pragma once

#include <imgui.h>

namespace engine {
    class RenderSystem;
    struct PlayerState;

    class GameUi {
    public:
        static void ResetTransientState();
        static void SetViewport(const ImVec2& viewportPos, const ImVec2& viewportSize, ImDrawList* drawList);
        static ImVec2 GetViewportPos();
        static ImVec2 GetViewportSize();
        static ImDrawList* GetViewportDrawList();
        static void DrawHud(RenderSystem* renderSys, const PlayerState& player, const ImVec2& viewportPos, const ImVec2& viewportSize);
    };
} // namespace engine
