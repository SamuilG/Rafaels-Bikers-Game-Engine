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
#include "EntityValidity.hpp"
#include "SceneHierarchy.hpp"
#include "../Physics/PhysicsSystem.hpp"
#include "../Physics/PhysicsTransform.hpp"
#include <Jolt/Physics/Collision/Shape/ScaledShape.h>
#include <glm/gtx/matrix_decompose.hpp>
#include <algorithm>
#include <filesystem>
#include <unordered_map>

namespace engine {

class SceneManager;

struct EditorEntitySnapshot {
    flecs::entity_t id = 0;
    std::string name;
    bool alive = false;
    bool visible = true;
    bool hasTransform = false;
    glm::mat4 transform = glm::mat4(1.0f);
    glm::mat4 worldTransform = glm::mat4(1.0f);
    flecs::entity_t parent = 0;
    bool group = false;
};

// Editor-only scene operations. Runtime gameplay and SceneManager remain the
// owners of the world; this adapter owns only editor selection and picking.
class EditorSceneAdapter final {
public:
    explicit EditorSceneAdapter(SceneManager* scene = nullptr) : mScene(scene) {}

    void SetScene(SceneManager* scene) { if (scene != mScene) ClearSelection(); mScene = scene; }
    SceneManager* Scene() const { return mScene; }

    int EntityCount() const { return static_cast<int>(ListEntities().size()); }

    EditorEntitySnapshot Inspect(flecs::entity_t id) const {
        EditorEntitySnapshot snapshot;
        snapshot.id = id;
        if (!mScene || id == 0) return snapshot;
        flecs::entity entity = mScene->get_world().entity(id);
        snapshot.alive = IsEntityAlive(entity);
        if (!snapshot.alive) return snapshot;
        snapshot.name = entity.name().size() > 0
            ? entity.name().c_str() : "ID: " + std::to_string(id);
        if (entity.has<EntityStatus>()) snapshot.visible = entity.get<EntityStatus>().should_render;
        if (entity.has<LocalTransform>()) {
            snapshot.hasTransform = true;
            snapshot.transform = entity.get<LocalTransform>().matrix;
            snapshot.worldTransform = scene_hierarchy::World(entity);
            snapshot.parent = entity.parent().id();
            snapshot.group = scene_hierarchy::IsGroup(entity);
        }
        return snapshot;
    }

    std::vector<EditorEntitySnapshot> ListEntities() const {
        std::vector<EditorEntitySnapshot> entities;
        if (!mScene) return entities;
        mScene->get_world().each([&](flecs::entity entity, const LocalTransform&) {
            if (entity.name() == "Preview_Object") return;
            auto snapshot = Inspect(entity.id());
            if (snapshot.alive) entities.push_back(std::move(snapshot));
        });
        std::sort(entities.begin(), entities.end(), [](const auto& a, const auto& b) { return a.id < b.id; });
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
    void ClearSelection() { mSelectedEntityId = mLastSelection = mRangeAnchor = 0; mSelectedEntities.clear(); }
    void SyncSelection() {
        if (mLastSelection != mSelectedEntityId) {
            mSelectedEntities.clear();
            if (IsAlive(mSelectedEntityId)) mSelectedEntities.push_back(mSelectedEntityId);
            mRangeAnchor = mSelectedEntityId;
        }
        std::erase_if(mSelectedEntities, [&](auto id) { return !IsAlive(id); });
        if (!IsAlive(mRangeAnchor)) mRangeAnchor = IsAlive(mSelectedEntityId) ? mSelectedEntityId : 0;
        mLastSelection = mSelectedEntityId;
    }
    void SelectRange(flecs::entity_t id, const std::vector<flecs::entity_t>& visibleOrder, bool additive = false) {
        SyncSelection();
        const auto target = std::find(visibleOrder.begin(), visibleOrder.end(), id);
        if (target == visibleOrder.end() || !IsAlive(id)) return;
        auto anchor = std::find(visibleOrder.begin(), visibleOrder.end(), mRangeAnchor);
        if (anchor == visibleOrder.end()) { anchor = target; mRangeAnchor = id; }
        if (!additive) mSelectedEntities.clear();
        const auto first = std::min(anchor, target), last = std::max(anchor, target);
        for (auto item = first; item != last + 1; ++item)
            if (IsAlive(*item) && std::find(mSelectedEntities.begin(), mSelectedEntities.end(), *item) == mSelectedEntities.end())
                mSelectedEntities.push_back(*item);
        // Shift extends from the original anchor, even when repeated backwards.
        mSelectedEntityId = mLastSelection = id;
    }
    void Select(flecs::entity_t id, bool additive = false) {
        SyncSelection();
        if (!additive) mSelectedEntities.clear();
        auto found = std::find(mSelectedEntities.begin(), mSelectedEntities.end(), id);
        if (additive && found != mSelectedEntities.end()) mSelectedEntities.erase(found);
        else if (IsAlive(id)) mSelectedEntities.push_back(id);
        mSelectedEntityId = mSelectedEntities.empty() ? 0 : mSelectedEntities.back();
        mLastSelection = mSelectedEntityId;
        mRangeAnchor = id;
    }
    const std::vector<flecs::entity_t>& SelectedIds() { SyncSelection(); return mSelectedEntities; }
    bool IsSelected(flecs::entity_t id) { const auto& ids = SelectedIds(); return std::find(ids.begin(), ids.end(), id) != ids.end(); }
    const std::string& Error() const { return mError; }

    flecs::entity SelectedEntity() const {
        return mScene && mSelectedEntityId != 0
            ? mScene->get_world().entity(mSelectedEntityId) : flecs::entity::null();
    }

    bool SetVisible(flecs::entity_t id, bool visible) {
        if (!mScene || id == 0) return false;
        flecs::entity entity = mScene->get_world().entity(id);
        if (!IsEntityAlive(entity) || !entity.has<EntityStatus>()) return false;
        entity.get_mut<EntityStatus>().should_render = visible;
        entity.modified<EntityStatus>();
        return true;
    }

    bool SetTransform(flecs::entity_t id, const glm::mat4& transform, bool syncPhysics = true) {
        if (!mScene || id == 0) return false;
        flecs::entity entity = mScene->get_world().entity(id);
        if (!IsEntityAlive(entity) || !entity.has<LocalTransform>() || !scene_hierarchy::Invertible(transform)) return false;
        std::unordered_map<flecs::entity_t, glm::mat4> previous;
        for (auto node : scene_hierarchy::Subtree(entity)) if (node.has<PhysicsBody>()) previous[node.id()] = scene_hierarchy::World(node);
        const auto delta = scene_hierarchy::World(entity.parent()) * transform * glm::inverse(scene_hierarchy::World(entity));
        for (const auto& [node, world] : previous)
            if (!DecomposePhysicsTransform(delta * world)) return Fail("The resulting collision scale is too small or invalid.");
        if (entity.has<GameCameraComponent>()) {
            auto& camera = entity.get_mut<GameCameraComponent>();
            if (camera.settings.mode != CameraMode::Free) return false;
            camera.freeTransform = transform;
            camera.settingsDirty = true;
            entity.modified<GameCameraComponent>();
        }
        entity.get_mut<LocalTransform>().matrix = transform;
        entity.modified<LocalTransform>();
        scene_hierarchy::Refresh(entity);
        if (syncPhysics) SyncPhysics(entity, previous);
        return true;
    }

    bool ApplyTransform(flecs::entity_t id, const glm::mat4& transform) {
        return SetTransform(id, transform, true);
    }

    bool SetWorldTransform(flecs::entity_t id, const glm::mat4& transform) {
        if (!IsAlive(id)) return false;
        const auto parentWorld = scene_hierarchy::World(mScene->get_world().entity(id).parent());
        return scene_hierarchy::Invertible(parentWorld) && SetTransform(id, glm::inverse(parentWorld) * transform);
    }

    bool CanReparent(const std::vector<flecs::entity_t>& ids, flecs::entity_t parent = 0) {
        mError.clear();
        if (!mScene || ids.empty() || (parent && !IsAlive(parent))) return Fail("Select entities to group or reparent.");
        auto target = parent ? mScene->get_world().entity(parent) : flecs::entity::null();
        if (parent && (!target.has<LocalTransform>() || !scene_hierarchy::Invertible(scene_hierarchy::World(target))))
            return Fail("The parent needs an invertible transform.");
        for (auto id : ids) {
            if (!IsAlive(id)) return Fail("The selection contains a deleted entity.");
            auto entity = mScene->get_world().entity(id);
            if (!entity.has<LocalTransform>() || (parent && scene_hierarchy::IsDescendant(target, entity)))
                return Fail("An entity cannot be parented to itself or its descendants.");
            for (auto node : scene_hierarchy::Subtree(entity))
                if (node.has<GameCameraComponent>() || node.has<CompoundParent>() || node.has<RiderBinding>())
                    return Fail("PlayerCamera and native vehicle attachments must keep their existing hierarchy.");
            unsigned parentDepth = 0;
            for (auto ancestor = target; IsEntityAlive(ancestor); ancestor = ancestor.parent()) ++parentDepth;
            for (auto node : scene_hierarchy::Subtree(entity)) {
                unsigned depth = parentDepth;
                for (auto ancestor = node; ancestor != entity && IsEntityAlive(ancestor); ancestor = ancestor.parent()) ++depth;
                if (depth >= 256) return Fail("The hierarchy would exceed 256 levels.");
            }
        }
        return true;
    }

    bool Reparent(const std::vector<flecs::entity_t>& ids, flecs::entity_t parent = 0) {
        if (!CanReparent(ids, parent)) return false;
        auto target = parent ? mScene->get_world().entity(parent) : flecs::entity::null();
        const auto inverseParent = glm::inverse(scene_hierarchy::World(target));
        struct Change { flecs::entity entity; glm::mat4 local; };
        std::vector<Change> changes;
        for (auto id : Roots(ids)) {
            auto entity = mScene->get_world().entity(id);
            const auto local = inverseParent * scene_hierarchy::World(entity);
            if (!scene_hierarchy::Invertible(local)) return Fail("Reparenting would produce an invalid transform.");
            changes.push_back({entity, local});
        }
        for (auto& change : changes) {
            auto entity = change.entity;
            const auto name = scene_hierarchy::UniqueName(mScene->get_world(), target, entity.name().c_str(), entity.id());
            entity.set_name(nullptr);
            entity.remove(flecs::ChildOf, flecs::Wildcard);
            if (parent) entity.child_of(target);
            entity.set_name(name.c_str());
            entity.set<LocalTransform>({change.local});
            scene_hierarchy::Refresh(entity);
        }
        return true;
    }

    flecs::entity CreateGroup(const std::vector<flecs::entity_t>& ids = {}, std::string name = "Group",
        const glm::mat4* importTransform = nullptr) {
        if (!mScene || (!ids.empty() && !CanReparent(ids))) return flecs::entity::null();
        const auto roots = Roots(ids);
        auto parent = roots.empty() ? flecs::entity::null() : mScene->get_world().entity(roots.front()).parent();
        for (auto id : roots) if (mScene->get_world().entity(id).parent() != parent) parent = flecs::entity::null();
        glm::mat4 local(1.0f);
        if (importTransform) { parent = flecs::entity::null(); local = *importTransform; }
        else if (!roots.empty()) {
            glm::vec3 minimum, maximum;
            bool first = true;
            for (auto id : roots) {
                glm::vec3 a, b;
                WorldBounds(id, a, b);
                minimum = first ? a : glm::min(minimum, a); maximum = first ? b : glm::max(maximum, b); first = false;
            }
            local[3] = glm::inverse(scene_hierarchy::World(parent)) * glm::vec4((minimum + maximum) * 0.5f, 1);
        }
        if (!scene_hierarchy::Invertible(local)) { Fail("The group needs an invertible transform."); return flecs::entity::null(); }
        auto group = mScene->get_world().entity();
        if (IsEntityAlive(parent)) group.child_of(parent);
        group.set_name(scene_hierarchy::UniqueName(mScene->get_world(), parent, name).c_str());
        group.set<LocalTransform>({local}).set<WorldTransform>({scene_hierarchy::World(parent) * local})
            .set<EntityStatus>({true, false}).set<SceneEntityIdentity>({NewSceneIdentity()});
        if (!roots.empty() && !Reparent(roots, group.id())) { group.destruct(); return flecs::entity::null(); }
        Select(group.id());
        return group;
    }

    flecs::entity GroupImportedModel(flecs::entity_t part, const glm::mat4* transform = nullptr) {
        if (!IsAlive(part)) return flecs::entity::null();
        auto entity = mScene->get_world().entity(part);
        if (!entity.has<ModelPartSource>()) return flecs::entity::null();
        const auto importId = entity.get<ModelPartSource>().instance;
        std::vector<flecs::entity_t> ids;
        mScene->get_world().each([&](flecs::entity node, const ModelPartSource& source) { if (source.instance == importId) ids.push_back(node.id()); });
        if (!transform && !ids.empty()) {
            const auto parent = entity.parent();
            bool sameParent = scene_hierarchy::IsGroup(parent);
            for (auto id : ids) sameParent &= mScene->get_world().entity(id).parent() == parent;
            if (sameParent) { Select(parent.id()); return parent; }
        }
        std::string name = "Model";
        for (const auto& source : mScene->ModelImports()) if (source.id == importId) {
            const auto bytes = std::filesystem::u8path(source.asset).stem().u8string();
            name.assign(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        }
        return CreateGroup(ids, name, transform);
    }

    bool Ungroup(flecs::entity_t id) {
        if (!IsAlive(id)) return false;
        auto group = mScene->get_world().entity(id);
        if (!scene_hierarchy::IsGroup(group)) return Fail("Only empty container entities can be ungrouped.");
        std::vector<flecs::entity_t> children;
        group.children([&](flecs::entity child) { children.push_back(child.id()); });
        if (!children.empty() && !Reparent(children, group.parent().id())) return false;
        mScene->DestroyEntity(group);
        Select(children.empty() ? 0 : children.front());
        return true;
    }

    bool Rename(flecs::entity_t id, const std::string& name) {
        const auto first = name.find_first_not_of(" \t\r\n");
        if (!IsAlive(id) || first == std::string::npos || name.find("::") != std::string::npos)
            return Fail("Enter a non-empty name without ::.");
        const auto clean = name.substr(first, name.find_last_not_of(" \t\r\n") - first + 1);
        auto entity = mScene->get_world().entity(id);
        if (entity.has<GameCameraComponent>()) return false;
        entity.set_name(scene_hierarchy::UniqueName(mScene->get_world(), entity.parent(), clean, id).c_str());
        mError.clear();
        return true;
    }

    bool WorldBounds(flecs::entity_t id, glm::vec3& minimum, glm::vec3& maximum) const {
        if (!IsAlive(id)) return false;
        bool first = true;
        for (auto node : scene_hierarchy::Subtree(mScene->get_world().entity(id))) {
            if (!node.has<MeshComponent>()) continue;
            const auto index = node.get<MeshComponent>().meshIndex;
            if (index >= mScene->get_model().meshes.size()) continue;
            const auto& mesh = mScene->get_model().meshes[index];
            const auto world = scene_hierarchy::World(node);
            for (int i = 0; i < 8; ++i) {
                const auto point = glm::vec3(world * glm::vec4(i & 1 ? mesh.localAabbMax.x : mesh.localAabbMin.x,
                    i & 2 ? mesh.localAabbMax.y : mesh.localAabbMin.y, i & 4 ? mesh.localAabbMax.z : mesh.localAabbMin.z, 1));
                minimum = first ? point : glm::min(minimum, point); maximum = first ? point : glm::max(maximum, point); first = false;
            }
        }
        if (first) {
            const glm::vec3 position(scene_hierarchy::World(mScene->get_world().entity(id))[3]);
            minimum = position - glm::vec3(0.5f); maximum = position + glm::vec3(0.5f);
        }
        return true;
    }

    bool Destroy(flecs::entity_t id) {
        if (!mScene || id == 0) return false;
        flecs::entity entity = mScene->get_world().entity(id);
        if (!IsEntityAlive(entity)) return false;
        for (auto child : scene_hierarchy::Subtree(entity)) if (child.has<GameCameraComponent>()) return false;
        mScene->DestroyEntity(entity);
        if (!IsAlive(mSelectedEntityId)) ClearSelection();
        return true;
    }

    bool IsAlive(flecs::entity_t id) const {
        return mScene && id != 0 && IsEntityAlive(mScene->get_world().entity(id));
    }

    bool TryGetDebugBodyId(flecs::entity_t id, uint32_t& outBodyId) const {
        if (!mScene || id == 0) return false;
        flecs::entity entity = mScene->get_world().entity(id);
        if (!IsEntityAlive(entity)) return false;
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
    bool Fail(const std::string& message) { mError = message; return false; }
    std::vector<flecs::entity_t> Roots(const std::vector<flecs::entity_t>& ids) const {
        std::vector<flecs::entity_t> roots;
        for (auto id : ids) {
            bool nested = false;
            for (auto other : ids) if (other != id && scene_hierarchy::IsDescendant(mScene->get_world().entity(id), mScene->get_world().entity(other))) { nested = true; break; }
            if (!nested && std::find(roots.begin(), roots.end(), id) == roots.end()) roots.push_back(id);
        }
        return roots;
    }
    void SyncPhysics(flecs::entity root, const std::unordered_map<flecs::entity_t, glm::mat4>& previous) {
        auto* physics = mScene->get_physics_system();
        if (!physics || !physics->get_system()) return;
        for (auto entity : scene_hierarchy::Subtree(root)) {
            if (!entity.has<PhysicsBody>()) continue;
            const auto bodyId = entity.get<PhysicsBody>().bodyID;
            const auto world = scene_hierarchy::World(entity);
            const auto pose = DecomposePhysicsTransform(world);
            if (!pose) continue;
            auto found = previous.find(entity.id());
            const auto oldPose = found != previous.end() ? DecomposePhysicsTransform(found->second) : std::nullopt;
            if (!oldPose || glm::length(pose->scale - oldPose->scale) > 1e-7f) {
                glm::vec3 shapeScale(1);
                const auto shape = physics->get_body_interface().GetShape(JPH::BodyID(bodyId));
                if (shape->GetSubType() == JPH::EShapeSubType::Scaled) {
                    const auto current = static_cast<const JPH::ScaledShape*>(shape.GetPtr())->GetScale();
                    shapeScale = {current.GetX(), current.GetY(), current.GetZ()};
                }
                // Convex imports bake their initial scale into their vertices;
                // mesh imports use a scaled wrapper. Scale relative to the
                // previous pose so both retain their original collision shape.
                shapeScale *= oldPose ? pose->scale / oldPose->scale : pose->scale;
                physics->set_body_scale(bodyId, shapeScale, pose->position, pose->rotation);
            } else physics->set_body_transform(bodyId, world);
            physics->get_body_interface().SetLinearAndAngularVelocity(JPH::BodyID(bodyId), JPH::Vec3::sZero(), JPH::Vec3::sZero());
        }
    }
    SceneManager* mScene = nullptr;
    flecs::entity_t mSelectedEntityId = 0;
    flecs::entity_t mLastSelection = 0;
    flecs::entity_t mRangeAnchor = 0;
    std::vector<flecs::entity_t> mSelectedEntities;
    std::string mError;
};

} // namespace engine
