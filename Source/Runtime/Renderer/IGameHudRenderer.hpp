#pragma once

#include <imgui.h>

namespace engine {
    class RenderSystem;
    struct UserState;

    class IGameHudRenderer {
    public:
        virtual ~IGameHudRenderer() = default;
        virtual void DrawHud(RenderSystem* renderSys, const UserState& state, const ImVec2& viewportPos, const ImVec2& viewportSize) = 0;
    };
} // namespace engine
