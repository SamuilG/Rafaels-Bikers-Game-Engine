#pragma once
#include "GameplayState.hpp"
#include "EditorState.hpp"
#include "RuntimeUiState.hpp"

namespace engine {

struct RuntimeUiStateView {
    // Runtime game UI owns game flow and presentation preferences only.
    // Editor workspace state is intentionally kept out of this view.
    GameFlowController& gameFlow;
    const PlayerController& player;
    GameplayPreferences& preferences;
    RuntimeUiState& runtimeUi;
};

struct SceneStateView {
    const GameFlowController& gameFlow;
    const PlayerController& player;
    const CameraController& camera;
    const RenderSettings& render;
    const EditorState& editor;
};

// Rendering can consume player/level outputs but cannot revive the player or
// change level progress. Mutable flow/preferences are forwarded to runtime UI.
struct RendererStateView {
    GameFlowController& gameFlow;
    const PlayerController& player;
    CameraController& camera;
    const LevelState& level;
    RenderSettings& render;
    const RenderOverrides& renderOverrides;
    RenderCapabilities& capabilities;
    RenderStatistics& renderStats;
    EditorState& editor;
    RuntimeUiState& runtimeUi;
    GameplayPreferences& preferences;

    RuntimeUiStateView RuntimeUi() { return {gameFlow, player, preferences, runtimeUi}; }
};

}
