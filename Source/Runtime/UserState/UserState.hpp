#pragma once
#include "GameplayState.hpp"
#include "EditorState.hpp"
#include "RuntimeUiState.hpp"
#include "StateViews.hpp"

namespace engine {
// Composition root owned by Application. Subsystems receive narrower views.
struct UserState {
    GameFlowController gameFlow;
    PlayerController player;
    CameraController camera;
    LevelState level;
    GameplayPreferences preferences;
    RenderSettings render;
    RenderOverrides renderOverrides;
    RenderCapabilities capabilities;
    RenderStatistics renderStats;
    EditorState editor;
    RuntimeUiState runtimeUi;

    GameplayState Gameplay() { return {gameFlow, player, camera, level, renderOverrides, preferences}; }
    SceneStateView Scene() const { return {gameFlow, player, camera, render, editor}; }
    RendererStateView Renderer() { return {gameFlow, player, camera, level, render, renderOverrides, capabilities, renderStats, editor, runtimeUi, preferences}; }
    RuntimeUiStateView RuntimeUi() { return {gameFlow, player, preferences, editor, runtimeUi}; }

    // Scene lifetimes end independently of device capabilities and user/workspace settings.
    void ResetSession() {
        player.ResetForNewRun();
        camera.ResetForNewRun();
        level = LevelState{};
        renderOverrides = RenderOverrides{};
        renderStats = RenderStatistics{};
        editor.activeParticleIndex = -1;
        editor.isSceneViewportHovered = false;
        editor.inputCapturesKeyboard = false;
        editor.inputCapturesMouse = false;
    }
};
}
