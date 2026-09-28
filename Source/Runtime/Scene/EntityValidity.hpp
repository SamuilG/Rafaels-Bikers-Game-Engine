#pragma once

#include <flecs.h>

namespace engine {

// Flecs permits handles with a world and id zero (empty selection or failed
// lookup), but ecs_is_alive asserts on id zero in Debug builds.
inline bool IsEntityAlive(const flecs::entity_view& entity) {
    return entity.id() != 0 && entity.is_alive();
}

} // namespace engine
