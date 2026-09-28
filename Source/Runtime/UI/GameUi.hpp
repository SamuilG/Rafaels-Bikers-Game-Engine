#pragma once

#include <imgui.h>
#include <functional>

namespace engine {
    struct HudData;

    class GameUi {
    public:
        using TextureResolver = std::function<ImTextureID(const char*)>;
        static void ResetTransientState();
        static void DrawHud(const HudData& hud, const ImVec2& viewportPos, const ImVec2& viewportSize, const TextureResolver& resolveTexture);
    };
} // namespace engine
