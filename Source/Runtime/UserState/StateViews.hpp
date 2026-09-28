#pragma once
#include "GameFlowController.hpp"
#include "PlayerController.hpp"
#include "CameraController.hpp"
#include "GameplayPreferences.hpp"
#include "RenderSettings.hpp"
#include "FrameExecution.hpp"
#include "../Gameplay/HudData.hpp"
#include "../Renderer/RenderUtilities/SceneEffects.hpp"
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
    const FrameExecution& frame;
    const PlayerController& player;
    const CameraController& camera;
    const RenderSettings& render;
    const EditorState& editor;
};

// Rendering consumes immutable game presentation outputs.
// Mutable flow/preferences are forwarded to runtime UI.
struct RendererStateView {
    GameFlowController& gameFlow;
    const PlayerController& player;
    CameraController& camera;
    const HudData& hud;
    const SceneEffects& effects;
    RenderSettings& render;
    const RenderOverrides& renderOverrides;
    RenderCapabilities& capabilities;
    RenderStatistics& renderStats;
    EditorState& editor;
    RuntimeUiState& runtimeUi;
    GameplayPreferences& preferences;

    const FrameExecution& frame;

    RuntimeUiStateView RuntimeUi() { return {gameFlow, player, preferences, runtimeUi}; }
};

}
