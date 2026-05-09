#include "GameFlowRenderer.hpp"
#include "GameFlowUi.hpp"

#include "../../Runtime/UserState/UserState.hpp"

namespace engine {
    bool GameFlowRenderer::DrawBlockingUI(RenderSystem* renderSys, UserState& state, bool& appRunning) {
        if (!state.gameplay.isGameStarted) {
            GameFlowUi::DrawMainMenu(renderSys, appRunning, state.gameplay.isGameStarted);
            return true;
        }

        if (state.gameplay.isGameOver) {
            GameFlowUi::DrawGameOver(renderSys, state, appRunning);
            return true;
        }

        if (state.gameplay.isGamePause) {
            GameFlowUi::DrawGamePause(renderSys, state, appRunning);
            return true;
        }

        return false;
    }
} // namespace engine
