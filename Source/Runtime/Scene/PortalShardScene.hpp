#pragma once

#include "SceneHierarchy.hpp"
#include <string>
#include <unordered_set>

namespace engine {

inline flecs::entity CreatePortalShardEffect(flecs::world& world, const char* path,
    const glm::mat4& transform, const PortalShardEmitter& emitter = {}) {
    return world.entity(path)
        .set<LocalTransform>({transform})
        .set<WorldTransform>({transform})
        .set<EntityStatus>({true, false})
        .set<PortalShardEmitter>(emitter);
}

inline void AddPortalShardAuthoringPath(std::unordered_set<std::string>& paths, std::string path) {
    while (paths.insert(path).second) {
        const auto separator = path.rfind("::");
        if (separator == std::string::npos || separator == 0) break;
        path.resize(separator);
    }
}

inline void RemoveMissingPortalShardEffects(SceneManager& scene, const std::unordered_set<std::string>& retained) {
    std::vector<flecs::entity> removed;
    scene.get_world().each([&](flecs::entity entity, const PortalShardEmitter&) {
        if (!retained.contains(entity.path().c_str())) removed.push_back(entity);
    });
    for (auto entity : removed) if (IsEntityAlive(entity)) scene.DestroyEntity(entity);
}

} // namespace engine
