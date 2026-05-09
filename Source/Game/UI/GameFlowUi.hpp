#pragma once

namespace engine {
    class RenderSystem;
    struct UserState;

    class GameFlowUi {
    public:
        static void DrawMainMenu(RenderSystem* renderSys, bool& appRunning, bool& isGameStarted);
        static void DrawGameOver(RenderSystem* renderSys, UserState& state, bool& appRunning);
        static void DrawGamePause(RenderSystem* renderSys, UserState& state, bool& appRunning);
    };
} // namespace engine
