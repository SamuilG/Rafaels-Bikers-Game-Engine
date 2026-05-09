#include "GameHudRenderer.hpp"

#include "GameUi.hpp"

namespace engine {
    void GameHudRenderer::DrawHud(RenderSystem* renderSys, const UserState& state, const ImVec2& viewportPos, const ImVec2& viewportSize) {
        GameUi::DrawHud(renderSys, state, viewportPos, viewportSize);
    }
} // namespace engine
