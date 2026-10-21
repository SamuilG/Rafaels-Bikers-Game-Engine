#pragma once

#include "SceneManager.hpp"
#include "EntityValidity.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <vector>

namespace engine::scene_hierarchy {
inline glm::mat4 World(flecs::entity entity) {
    glm::mat4 result(1.0f);
    unsigned depth = 0;
    while (IsEntityAlive(entity)) {
        if (++depth > 256) throw std::runtime_error("Scene hierarchy exceeds 256 levels");
        if (entity.has<LocalTransform>()) result = entity.get<LocalTransform>().matrix * result;
        entity = entity.parent();
    }
    return result;
}
inline bool Invertible(const glm::mat4& matrix) {
    for (int col = 0; col < 4; ++col) for (int row = 0; row < 4; ++row)
        if (!std::isfinite(matrix[col][row]) || std::abs(matrix[col][row]) > 1e8f) return false;
    return std::abs(matrix[0][3]) < 1e-5f && std::abs(matrix[1][3]) < 1e-5f &&
        std::abs(matrix[2][3]) < 1e-5f && std::abs(matrix[3][3] - 1) < 1e-5f &&
        std::abs(glm::determinant(glm::mat3(matrix))) >= 1e-12f;
}
inline bool IsDescendant(flecs::entity entity, flecs::entity ancestor) {
    unsigned depth = 0;
    while (IsEntityAlive(entity) && ++depth <= 256) {
        if (entity == ancestor) return true;
        entity = entity.parent();
    }
    return false;
}
inline std::vector<flecs::entity> Subtree(flecs::entity root) {
    std::vector<flecs::entity> result;
    if (!IsEntityAlive(root)) return result;
    result.push_back(root);
    for (std::size_t i = 0; i < result.size(); ++i) {
        const auto parent = result[i];
        parent.children([&](flecs::entity child) { result.push_back(child); });
    }
    return result;
}
inline void Refresh(flecs::entity root) {
    for (auto entity : Subtree(root)) if (entity.has<LocalTransform>()) entity.set<WorldTransform>({World(entity)});
}
inline bool Visible(flecs::entity entity) {
    unsigned depth = 0;
    while (IsEntityAlive(entity) && ++depth <= 256) {
        if (entity.has<EntityStatus>() && !entity.get<EntityStatus>().should_render) return false;
        entity = entity.parent();
    }
    return depth <= 256;
}
inline bool IsGroup(flecs::entity entity) {
    return IsEntityAlive(entity) && entity.has<LocalTransform>() && !entity.has<MeshComponent>() &&
        !entity.has<GameCameraComponent>() && !entity.has<LightComponent>();
}
inline std::string UniqueName(flecs::world& world, flecs::entity parent, std::string desired, flecs::entity_t self = 0) {
    if (desired.empty() || desired.find("::") != std::string::npos) desired = "Group";
    std::string candidate = desired;
    const auto exists = [&](const std::string& name) {
        bool used = false;
        world.each([&](flecs::entity entity, const LocalTransform&) {
            if (entity.id() != self && entity.parent().id() == parent.id() && entity.name() == name.c_str()) used = true;
        });
        return used;
    };
    for (unsigned i = 1; exists(candidate); ++i) candidate = desired + "_" + std::to_string(i);
    return candidate;
}
}
