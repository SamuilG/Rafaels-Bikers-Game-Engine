#pragma once

#include "../UserState/FrameExecution.hpp"
#include <functional>

namespace engine {

// Application supplies the concrete systems. Keeping the schedule independent
// of GLFW/Vulkan lets regressions exercise the actual production frame order.
struct FrameSteps {
    std::function<void(float)> gameplay;
    std::function<void(float)> events;
    std::function<void(float)> physics;
    std::function<void(float)> postPhysics;
    std::function<void(float)> scene;
    std::function<void(float)> animation;
    std::function<void(float)> gameEffects;
    std::function<void(float)> presentation;
    std::function<void(float)> audio;
};

class FrameScheduler {
public:
    // The only production commit boundary. Requests submitted by any callback
    // below remain pending until the next invocation. Reload completion may
    // publish Playing, but the rebuilt scene never simulates a partial frame.
    template<typename Reload>
    static FrameExecution BeginFrame(GameFlowController& flow, Reload&& reload, bool editingProject = false) {
        flow.CommitPendingRequests();
        const bool reloadHandled = flow.TakeReloadRequest();
        if (reloadHandled) flow.CompleteReload(reload());
        return FrameExecution::Plan(flow, reloadHandled, editingProject);
    }

    static void Execute(const FrameExecution& frame, float dt, const FrameSteps& steps) {
        if (frame.runGameplay) Invoke(steps.gameplay, dt);
        if (frame.runSimulationSystems) {
            Invoke(steps.events, dt);
            Invoke(steps.physics, dt);
            Invoke(steps.postPhysics, dt);
        }
        // Editor transform propagation stays alive. Scene physics sync reads
        // the same immutable frame rather than querying the mutable flow host.
        Invoke(steps.scene, dt);
        if (frame.runSimulationSystems) Invoke(steps.animation, dt);
        if (frame.runSimulationSystems) Invoke(steps.gameEffects, dt);
        if (frame.runPresentation) Invoke(steps.presentation, dt);
        Invoke(steps.audio, dt);
    }

private:
    static void Invoke(const std::function<void(float)>& step, float dt) {
        if (step) step(dt);
    }
};

} // namespace engine
