#include "RenderDebugControls.hpp"

#include "../Input/InputSystem.hpp"
#include "../UI/EngineUi.hpp"
#include "../UserState/UserState.hpp"

#include <cmath>
#include <cstdio>

#include <imgui.h>

namespace engine {
    void RenderDebugControls::HandleUiDebugHotkeys(UserState& state) {
        if (ImGui::IsKeyPressed(ImGuiKey_G)) {
            state.gameplay.isGameOver = !state.gameplay.isGameOver;

            if (state.gameplay.isGameOver) {
                EngineUi::LogPrintf("Test: Game Over triggered via 'G' key.\n");
            }
            else {
                EngineUi::LogPrintf("Test: Back to Game/Menu.\n");
            }
        }

        if (ImGui::IsKeyPressed(ImGuiKey_H)) {
            state.gameplay.isGamePause = !state.gameplay.isGamePause;

            if (state.gameplay.isGamePause) {
                EngineUi::LogPrintf("Test: Game pause triggered via 'H' key.\n");
            }
            else {
                EngineUi::LogPrintf("Test: Back to Game/Menu.\n");
            }
        }
    }

    void RenderDebugControls::HandleRenderDebugActions(UserState& state, InputSystem& inputSystem) {
        if (inputSystem.IsActionPressed("Default")) state.renderMode = 0;
        if (inputSystem.IsActionPressed("DebugMipmap")) state.renderMode = 1;
        if (inputSystem.IsActionPressed("DebugDepth")) state.renderMode = 2;
        if (inputSystem.IsActionPressed("DebugDerivatives")) state.renderMode = 3;
        if (inputSystem.IsActionPressed("DebugMosaic")) state.mosaicEnabled = !state.mosaicEnabled;
        if (inputSystem.IsActionPressed("DebugOverdraw")) state.renderMode = 4;
        if (inputSystem.IsActionPressed("DebugOvershading")) state.renderMode = 5;
        if (inputSystem.IsActionPressed("DebugShadows")) state.renderMode = 6;

        if (inputSystem.IsActionPressed("PrintCameraPos")) {
            auto const pos = state.camera.camera2world[3];
            std::printf("Camera Pos: %.4f, %.4f, %.4f\n", pos.x, pos.y, pos.z);
        }
    }
} // namespace engine
