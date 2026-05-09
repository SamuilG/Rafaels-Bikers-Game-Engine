#pragma once

#include "../../Runtime/Renderer/IGameFlowRenderer.hpp"

namespace engine {
    class GameFlowRenderer final : public IGameFlowRenderer {
    public:
        bool DrawBlockingUI(RenderSystem* renderSys, UserState& state, bool& appRunning) override;
    };
} // namespace engine
