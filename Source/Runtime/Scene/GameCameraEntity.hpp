#pragma once

#include <flecs.h>
#include "EntityValidity.hpp"
#include "TransformComponents.hpp"
#include "../UserState/CameraController.hpp"
#include "../UserState/CameraSettings.hpp"

namespace engine {

// Authoring data belongs to the entity. Runtime input, smoothing and temporary
// camera owners never write these settings back into the saved scene.
struct GameCameraComponent {
    CameraSettings settings;
    glm::mat4 freeTransform = CameraState{}.camera2world;
    bool settingsDirty = false;
};

inline constexpr const char* kGameCameraEntityName = "PlayerCamera";

inline void InitializeDefaultGameCamera(CameraController& camera, const PlayerState& player,
    const glm::vec3& position = {0.0f, 2.0f, 5.0f}, const glm::vec3& target = {0.0f, 2.0f, 0.0f}) {
    CameraSettings settings;
    settings.mode = CameraMode::Free;
    settings.automaticFov = false;
    settings.fov = 75.0f;
    camera.ApplySettings(settings,
        glm::inverse(glm::lookAt(position, target, glm::vec3(0.0f, 1.0f, 0.0f))), player);
}

inline flecs::entity FindGameCameraEntity(flecs::world& world) {
    const auto entity = world.lookup(kGameCameraEntityName);
    return IsEntityAlive(entity) && entity.has<GameCameraComponent>()
        ? entity : flecs::entity::null();
}

inline flecs::entity EnsureGameCameraEntity(flecs::world& world, const CameraController& camera) {
    if (auto entity = FindGameCameraEntity(world); IsEntityAlive(entity)) return entity;
    return world.entity(kGameCameraEntityName)
        .set<GameCameraComponent>({CameraSettings::FromState(camera.State()), camera.State().camera2world, false})
        .set<LocalTransform>({camera.State().camera2world})
        .set<WorldTransform>({camera.State().camera2world});
}

inline bool ApplyGameCameraEdits(flecs::world& world, CameraController& camera, const PlayerState& player) {
    const auto entity = FindGameCameraEntity(world);
    if (!IsEntityAlive(entity)) return false;
    auto& component = entity.get_mut<GameCameraComponent>();
    if (!component.settingsDirty || !camera.ApplySettings(component.settings, component.freeTransform, player))
        return false;
    component.settingsDirty = false;
    entity.modified<GameCameraComponent>();
    return true;
}

inline void PublishGameCameraPose(flecs::world& world, const CameraController& camera) {
    const auto entity = FindGameCameraEntity(world);
    if (!IsEntityAlive(entity)) return;
    entity.set<LocalTransform>({camera.State().camera2world});
    entity.set<WorldTransform>({camera.State().camera2world});
}

// Moving the default camera while editing changes the authored scene pose.
// Play-mode input continues to publish only its transient runtime transform.
inline void RecordGameCameraPreview(flecs::world& world, const CameraController& camera) {
    const auto entity = FindGameCameraEntity(world);
    if (!IsEntityAlive(entity) || camera.State().mode != CameraMode::Free) return;
    auto& component = entity.get_mut<GameCameraComponent>();
    if (component.settings.mode != CameraMode::Free) return;
    component.freeTransform = camera.State().camera2world;
    component.settings.fov = camera.State().targetFov;
    component.settings.automaticFov = camera.State().automaticFov;
    entity.modified<GameCameraComponent>();
}

inline void CopyGameCameraAuthoring(flecs::entity source, flecs::entity destination) {
    if (!IsEntityAlive(source) || !IsEntityAlive(destination) ||
        !source.has<GameCameraComponent>() || !destination.has<GameCameraComponent>()) return;
    auto component = source.get<GameCameraComponent>();
    component.settingsDirty = true;
    destination.set<GameCameraComponent>(component);
}

} // namespace engine
