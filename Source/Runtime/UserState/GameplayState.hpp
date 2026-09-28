#pragma once
#include "GameFlowController.hpp"
#include "PlayerController.hpp"
#include "CameraController.hpp"
#include "LevelState.hpp"
#include "GameplayPreferences.hpp"
#include "RenderSettings.hpp"
#include "FrameExecution.hpp"
#include "../Gameplay/HudController.hpp"
#include "../Gameplay/VehiclePresentation.hpp"

namespace engine {
// Game code can issue player/camera/flow commands and own its level data.
// It has no access to the editor workspace or writable user rendering preferences.
struct GameplayState {
    GameFlowController& gameFlow;
    PlayerController& player;
    CameraController& camera;
    LevelState& level;
    RenderOverrides& renderOverrides;
    const GameplayPreferences& preferences;
    const FrameExecution& frame;
    HudController& hud;
    VehiclePresentation& vehiclePresentation;
};
}
