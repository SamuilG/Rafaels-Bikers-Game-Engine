#pragma once

namespace engine {
    class RenderSystem;
    struct UserState;

    class IGameFlowRenderer {
    public:
        virtual ~IGameFlowRenderer() = default;

        // Returns true when the game flow UI consumes the frame and world/viewport rendering should be skipped.
        virtual bool DrawBlockingUI(RenderSystem* renderSys, UserState& state, bool& appRunning) = 0;
    };
} // namespace engine
