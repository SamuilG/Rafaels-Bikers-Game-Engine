#pragma once

#include <imgui.h>
#include <functional>

namespace engine {
    struct HudData;

    class GameUi {
    public:
        using TextureResolver = std::function<ImTextureID(const char*)>;
        static void ResetTransientState();
        static void SetViewport(const ImVec2& viewportPos, const ImVec2& viewportSize, ImDrawList* drawList);
        static ImVec2 GetViewportPos();
        static ImVec2 GetViewportSize();
        static ImDrawList* GetViewportDrawList();
        static void DrawHud(const HudData& hud, const ImVec2& viewportPos, const ImVec2& viewportSize, const TextureResolver& resolveTexture);
    };
} // namespace engine
