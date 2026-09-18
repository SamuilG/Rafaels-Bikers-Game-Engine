#pragma once

namespace engine {

// The application advances one frame through these gates in a fixed order.
// Presentation remains alive while simulation is paused or a reload is being
// completed, but gameplay systems never receive a partial simulation frame.
struct FrameExecution {
    bool reloadHandled = false;
    bool runGameplay = false;
    bool runSimulationSystems = false;
    bool runPresentation = true;

    static FrameExecution Plan(bool canSimulate, bool reloadWasHandled) {
        FrameExecution plan;
        plan.reloadHandled = reloadWasHandled;
        plan.runGameplay = canSimulate && !reloadWasHandled;
        plan.runSimulationSystems = plan.runGameplay;
        return plan;
    }
};

} // namespace engine
