#pragma once

#include "../../Runtime/Renderer/IGameHudRenderer.hpp"

namespace engine {
    class GameHudRenderer final : public IGameHudRenderer {
    public:
        void DrawHud(RenderSystem* renderSys, const UserState& state, const ImVec2& viewportPos, const ImVec2& viewportSize) override;
    };
} // namespace engine
