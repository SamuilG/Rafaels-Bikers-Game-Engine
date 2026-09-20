#pragma once

#include <cstdint>
#include <cmath>
#include <string>
#include <vector>
#include <utility>
#include <glm/glm.hpp>
#include <flecs.h>
#include "../UI/MousePicker.hpp"
#include "SceneManager.hpp"
#include "../Physics/PhysicsSystem.hpp"
#include <glm/gtx/matrix_decompose.hpp>

namespace engine {

class SceneManager;

struct EditorEntitySnapshot {
    flecs::entity_t id = 0;
    std::string name;
    bool alive = false;
    bool visible = true;
    bool hasTransform = false;
    glm::mat4 transform = glm::mat4(1.0f);
};

// Editor-only scene operations. Runtime gameplay and SceneManager remain the
// owners of the world; this adapter owns only editor selection and picking.
class EditorSceneAdapter final {
public:
    explicit EditorSceneAdapter(SceneManager* scene = nullptr) : mScene(scene) {}

    void SetScene(SceneManager* scene) { mScene = scene; }
    SceneManager* Scene() const { return mScene; }

    int EntityCount() const { return mScene ? mScene->get_entity_count() : 0; }

    EditorEntitySnapshot Inspect(flecs::entity_t id) const {
        EditorEntitySnapshot snapshot;
        snapshot.id = id;
        if (!mScene) return snapshot;
        flecs::entity entity = mScene->get_world().entity(id);
        snapshot.alive = entity.is_alive();
        if (!snapshot.alive) return snapshot;
        snapshot.name = entity.name().size() > 0
            ? entity.name().c_str() : "ID: " + std::to_string(id);
        if (entity.has<EntityStatus>()) snapshot.visible = entity.get<EntityStatus>().should_render;
        if (entity.has<LocalTransform>()) {
            snapshot.hasTransform = true;
            snapshot.transform = entity.get<LocalTransform>().matrix;
        }
        return snapshot;
    }

    std::vector<EditorEntitySnapshot> ListEntities() const {
        std::vector<EditorEntitySnapshot> entities;
        if (!mScene) return entities;
        entities.reserve(static_cast<std::size_t>(EntityCount()));
        mScene->get_world().each([&](flecs::entity entity, MeshComponent&) {
            EditorEntitySnapshot snapshot = Inspect(entity.id());
            if (snapshot.alive) entities.push_back(std::move(snapshot));
        });
        return entities;
    }

    flecs::entity Pick(float mouseX, float mouseY, float viewportWidth,
        float viewportHeight, const glm::mat4& camera2world,
        const glm::mat4& projection) {
        if (!mScene) {
            return flecs::entity::null();
        }
        return MousePicker::PickEntity(mouseX, mouseY, viewportWidth,
            viewportHeight, camera2world, projection, mScene);
    }

    flecs::entity_t& Selection() { return mSelectedEntityId; }
    flecs::entity_t Selection() const { return mSelectedEntityId; }
    void ClearSelection() { mSelectedEntityId = 0; }

    flecs::entity SelectedEntity() const {
        return mScene ? mScene->get_world().entity(mSelectedEntityId) : flecs::entity::null();
    }

    bool SetVisible(flecs::entity_t id, bool visible) {
        if (!mScene) return false;
        flecs::entity entity = mScene->get_world().entity(id);
        if (!entity.is_alive() || !entity.has<EntityStatus>()) return false;
        entity.get_mut<EntityStatus>().should_render = visible;
        entity.modified<EntityStatus>();
        return true;
    }

    bool SetTransform(flecs::entity_t id, const glm::mat4& transform, bool syncPhysics = true) {
        if (!mScene) return false;
        flecs::entity entity = mScene->get_world().entity(id);
        if (!entity.is_alive() || !entity.has<LocalTransform>()) return false;
        entity.get_mut<LocalTransform>().matrix = transform;
        entity.modified<LocalTransform>();
        if (syncPhysics && entity.has<PhysicsBody>()) {
            if (PhysicsSystem* physics = mScene->get_physics_system()) {
                physics->set_body_transform(entity.get<PhysicsBody>().bodyID, transform);
            }
        }
        return true;
    }

    bool ApplyTransform(flecs::entity_t id, const glm::mat4& transform) {
        if (!SetTransform(id, transform, true)) return false;
        flecs::entity entity = mScene->get_world().entity(id);
        if (!entity.has<PhysicsBody>()) return true;
        PhysicsSystem* physics = mScene->get_physics_system();
        if (!physics) return true;

        glm::vec3 scale{}, translation{}, skew{};
        glm::quat rotation{};
        glm::vec4 perspective{};
        if (!glm::decompose(transform, scale, rotation, translation, skew, perspective)) return true;
        for (int axis = 0; axis < 3; ++axis) {
            if (std::abs(scale[axis]) < 0.001f) scale[axis] = scale[axis] >= 0.0f ? 0.001f : -0.001f;
        }
        physics->set_body_scale(entity.get<PhysicsBody>().bodyID, scale, translation, rotation);
        physics->get_body_interface().SetLinearAndAngularVelocity(
            JPH::BodyID(entity.get<PhysicsBody>().bodyID), JPH::Vec3::sZero(), JPH::Vec3::sZero());
        return true;
    }

    bool Destroy(flecs::entity_t id) {
        if (!mScene) return false;
        flecs::entity entity = mScene->get_world().entity(id);
        if (!entity.is_alive()) return false;
        entity.destruct();
        if (mSelectedEntityId == id) ClearSelection();
        return true;
    }

    bool IsAlive(flecs::entity_t id) const {
        return mScene && mScene->get_world().entity(id).is_alive();
    }

    bool TryGetDebugBodyId(flecs::entity_t id, uint32_t& outBodyId) const {
        if (!mScene) return false;
        flecs::entity entity = mScene->get_world().entity(id);
        if (!entity.is_alive()) return false;
        if (entity.has<PhysicsBody>()) {
            outBodyId = entity.get<PhysicsBody>().bodyID;
            return true;
        }
        if (entity.has<CompoundParent>()) {
            outBodyId = entity.get<CompoundParent>().bodyID;
            return true;
        }
        return false;
    }

    bool TryGetDebugShape(flecs::entity_t id, JPH::TransformedShape& outShape) const {
        uint32_t bodyId = JPH::BodyID::cInvalidBodyID;
        PhysicsSystem* physics = mScene ? mScene->get_physics_system() : nullptr;
        if (!physics || !TryGetDebugBodyId(id, bodyId)) return false;
        outShape = physics->get_body_interface().GetTransformedShape(JPH::BodyID(bodyId));
        return true;
    }

private:
    SceneManager* mScene = nullptr;
    flecs::entity_t mSelectedEntityId = 0;
};

} // namespace engine
