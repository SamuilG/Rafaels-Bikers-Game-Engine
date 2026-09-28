#pragma once
#include "GameFlowController.hpp"

namespace engine {

// The application advances one frame through these gates in a fixed order.
// Presentation remains alive while simulation is paused or a reload is being
// completed, but gameplay systems never receive a partial simulation frame.
struct FrameExecution {
    GameFlowState flowState = GameFlowState::MainMenu;
    bool settingsOpen = false;
    std::uint64_t flowRevision = 0;
    bool reloadHandled = false;
    bool runGameplay = false;
    bool runSimulationSystems = false;
    bool runPresentation = true;

    static FrameExecution Plan(const GameFlowController& flow, bool reloadWasHandled, bool editingProject = false) {
        FrameExecution plan;
        plan.flowState = flow.State();
        plan.settingsOpen = flow.IsSettingsOpen();
        plan.flowRevision = flow.Revision();
        plan.reloadHandled = reloadWasHandled;
        plan.runGameplay = flow.CanSimulate() && !reloadWasHandled && !editingProject;
        plan.runSimulationSystems = plan.runGameplay;
        return plan;
    }
};

} // namespace engine
