#pragma once

#include <flecs.h>
#include "../UserState/FrameExecution.hpp"

namespace engine {

inline void UpdateSceneWorld(flecs::world& world, const flecs::system& transformSystem,
    const FrameExecution& frame, float dt) {
    if (frame.runSimulationSystems) {
        world.progress(dt);
    }
    else {
        // progress(0) measures wall time, and time_scale=0 still advances Flecs
        // raw-time timers. Run only editor transform propagation while stopped.
        transformSystem.run(0.0f);
    }
}

} // namespace engine
