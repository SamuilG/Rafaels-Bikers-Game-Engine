// Regression against the real Flecs scene extraction and production CSM math.
#include "Runtime/Scene/SceneRenderer.hpp"
#include "Runtime/Scene/SceneManager.hpp"
#include "Runtime/Scene/SceneHierarchy.hpp"
#include "Runtime/UserState/UserState.hpp"
#include <algorithm>
#include <cstdio>
#include <stdexcept>

using namespace engine;
static void Require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
static bool Contains(const std::vector<RenderBatch>& batches, flecs::entity entity) {
    return std::ranges::any_of(batches, [&](const auto& batch) { return batch.entityId == entity.id(); });
}
static flecs::entity Import(SceneManager& scene, const char* name, glm::vec3 position, uint32_t meshIndex) {
    EngineModel model;
    EngineMesh mesh;
    mesh.localAabbMin = glm::vec3(-0.25f); mesh.localAabbMax = glm::vec3(0.25f);
    model.meshes.push_back(mesh);
    model.scenes.push_back({0, glm::translate(glm::mat4(1), position), name});
    return scene.load_static_model(model, meshIndex, 0, RenderLayer::Default);
}
int main() {
    try {
        UserState state;
        auto sceneState = state.Scene();
        SceneManager scene;
        scene.SetState(&sceneState); scene.Init();
        SceneRenderer renderer(&scene);
        const glm::vec3 receiverPosition(0, 0, -5), casterPosition(5, 0, -5);
        const auto receiver = Import(scene, "receiver", receiverPosition, 0);
        const auto caster = Import(scene, "offscreen-caster", casterPosition, 1);
        const auto hidden = Import(scene, "hidden", casterPosition, 2);
        hidden.set<EntityStatus>({false, false});
        const auto emissive = Import(scene, "emissive", casterPosition, 3);
        emissive.set<LayerComponent>({RenderLayer::Emissive});
        const auto hiddenParent = scene.get_world().entity("hidden-parent").set<EntityStatus>({false, false});
        const auto child = Import(scene, "hidden-child", casterPosition, 4);
        child.child_of(hiddenParent);

        const auto projection = glm::perspective(glm::radians(60.0f), 1.0f, 0.1f, 100.0f);
        const auto cameraFrustum = BuildFrustum(projection);
        const auto visible = renderer.BuildOpaque({&cameraFrustum, 0, {0, 0, 0}});
        Require(Contains(visible, receiver) && !Contains(visible, caster), "fixture must cull the offscreen caster but retain its receiver");
        const auto candidates = renderer.LastFrustumCandidates(), count = renderer.LastFrustumVisible();
        Require(candidates == 3 && count == 1, "main camera statistics must describe camera-visible geometry");
        const auto shadow = renderer.BuildShadowCasters({0, 0, 0});
        Require(Contains(shadow, caster) && Contains(shadow, receiver), "offscreen casters must survive camera culling");
        Require(!Contains(shadow, hidden) && !Contains(shadow, child) && !Contains(shadow, emissive), "hidden entities, hidden descendants and non-casting layers stay excluded");
        Require(renderer.LastFrustumCandidates() == candidates && renderer.LastFrustumVisible() == count, "shadow extraction must not overwrite camera statistics");

        LightComponent sun;
        sun.type = LightType::Directional; sun.primaryDirectional = true;
        sun.direction = {-1, 0, 0}; // caster -> visible receiver along the light ray
        auto lighting = BuildSceneLighting({{1, "MainSun", glm::mat4(1), sun}});
        const auto frame = BuildDirectionalShadowFrame(lighting.directional, glm::mat4(1), glm::radians(60.0f), 1, 0.1f, 100);
        const auto projectedReceiver = frame.cascades.lightVP[0] * glm::vec4(receiverPosition, 1);
        const auto projectedCaster = frame.cascades.lightVP[0] * glm::vec4(casterPosition, 1);
        Require(glm::length(glm::vec2(projectedReceiver - projectedCaster)) < 0.0001f && projectedCaster.z < projectedReceiver.z,
            "offscreen fixture must occlude the visible receiver in the production directional shadow projection");
        Require(std::abs(projectedCaster.x) < 1 && std::abs(projectedCaster.y) < 1 && projectedCaster.z >= 0 && projectedReceiver.z <= 1,
            "fixture must be inside the directional shadow volume");
        const auto spotVP = compute_spotlight_matrix({10, 0, -5}, {-1, 0, 0}, 30, 20);
        const auto spotReceiver = spotVP * glm::vec4(receiverPosition, 1);
        const auto spotCaster = spotVP * glm::vec4(casterPosition, 1);
        Require(glm::length(glm::vec2(spotReceiver / spotReceiver.w - spotCaster / spotCaster.w)) < 0.0001f &&
            spotCaster.z / spotCaster.w < spotReceiver.z / spotReceiver.w,
            "the same offscreen caster must also occlude the receiver in the production spotlight projection");
        auto otherCamera = glm::lookAt(glm::vec3(0, 0, -10), receiverPosition, glm::vec3(0, 1, 0));
        const auto portalFrustum = BuildFrustum(projection * otherCamera);
        Require(!Contains(renderer.BuildOpaque({&portalFrustum, 0, {0, 0, -10}}), caster), "portal camera must independently cull the caster");
        Require(Contains(renderer.BuildShadowCasters({0, 0, 0}), caster), "portal shadows must have the same unculled caster source");
        const auto unculled = renderer.BuildOpaque({nullptr, 0, {0, 0, 0}});
        auto expected = unculled;
        std::erase_if(expected, [](const auto& batch) { return !batch.castShadow; });
        Require(shadow.size() == expected.size(), "caster set must match the culling-disabled reference");
        for (const auto& batch : shadow) Require(std::ranges::any_of(expected, [&](const auto& ref) {
            return ref.entityId == batch.entityId && ref.meshIndex == batch.meshIndex && ref.transform == batch.transform;
        }), "shadow geometry must remain identical with camera culling on or off");
        std::puts("PASS real scene: offscreen directional/spot caster, independent portal views, visibility rules and unchanged view statistics");

        state.render.lodEnabled = true;
        scene.SetupEntityLOD(caster, {0}, {2});
        const auto lodCasters = renderer.BuildShadowCasters({0, 0, 0});
        const auto lod = std::ranges::find_if(lodCasters, [&](const auto& batch) { return batch.entityId == caster.id(); });
        Require(lod != lodCasters.end() && lod->meshIndex == 0, "shadow preparation must retain authored distance-based LOD");
        const auto animated = Import(scene, "offscreen-skinned", casterPosition, 5);
        SkinComponent skin;
        skin.boneMatrices = {glm::translate(glm::mat4(1), glm::vec3(0, 1, 0))};
        animated.set<SkinComponent>(skin);
        glm::mat4 bones[8]; size_t boneCount = 0;
        auto skinned = renderer.BuildSkinned(bones, 8, boneCount);
        Require(Contains(skinned, animated) && boneCount == 1 && skinned[0].boneBaseIndex == 0 && bones[0] == skin.boneMatrices[0],
            "offscreen animated casters must retain the matching uploaded bone slice");
        Require(!Contains(renderer.BuildShadowCasters({0, 0, 0}), animated), "animated casters must not be duplicated in the static shadow list");
        animated.set<EntityStatus>({false, false});
        Require(renderer.BuildSkinned(bones, 8, boneCount).empty(), "hidden animated geometry must remain excluded");
        Require(SceneRenderer{}.BuildShadowCasters({0, 0, 0}).empty(), "empty scene has no shadow casters");
        std::puts("PASS real scene: shadow LOD and offscreen skinning preserve mesh and bone contracts");
        return 0;
    } catch (const std::exception& error) { std::fprintf(stderr, "FAIL: %s\n", error.what()); return 1; }
}
