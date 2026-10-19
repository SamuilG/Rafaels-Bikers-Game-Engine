#include "SceneDocument.hpp"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <map>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include "../Scene/SceneManager.hpp"
#include "../Scene/CameraSerialization.hpp"
#include "../Physics/PhysicsSystem.hpp"
#include "../Renderer/RenderSystem.hpp"

namespace engine {
namespace {
using Json = nlohmann::json;
namespace fs = std::filesystem;

Json Vector(const glm::vec3& value) { return Json::array({value.x, value.y, value.z}); }
Json Vector(const glm::vec4& value) { return Json::array({value.x, value.y, value.z, value.w}); }
glm::vec3 ReadVec3(const Json& value) { return {value[0].get<float>(), value[1].get<float>(), value[2].get<float>()}; }
glm::vec4 ReadVec4(const Json& value) { return {value[0].get<float>(), value[1].get<float>(), value[2].get<float>(), value[3].get<float>()}; }

Json Matrix(const glm::mat4& value) {
    Json result = Json::array();
    for (int column = 0; column < 4; ++column)
        for (int row = 0; row < 4; ++row) result.push_back(value[column][row]);
    return result;
}

glm::mat4 ReadMatrix(const Json& value) {
    glm::mat4 result(1.0f);
    for (int column = 0; column < 4; ++column)
        for (int row = 0; row < 4; ++row) result[column][row] = value[column * 4 + row].get<float>();
    return result;
}

std::string ResourcePath(const std::string& filename) {
    const auto assets = fs::weakly_canonical("Assets");
    const auto file = fs::weakly_canonical(fs::u8path(filename));
    const auto relative = file.lexically_relative(assets);
    if (relative.empty() || relative.is_absolute() || *relative.begin() == "..")
        throw std::runtime_error("Cannot save resource outside Assets: " + filename + ". Copy it into Assets and import it again.");
    const auto path = (fs::path("Assets") / relative).generic_u8string();
    return {reinterpret_cast<const char*>(path.data()), path.size()};
}

std::string TexturePath(RenderSystem& renderer, VkDescriptorSet descriptor, bool icon) {
    if (descriptor == VK_NULL_HANDLE) return {};
    for (const auto& path : renderer.GetParticleTextureNames()) {
        const auto candidate = icon ? renderer.GetImGuiTextureDescriptor(path) : renderer.GetParticleTextureDescriptor(path);
        if (candidate == descriptor) return ResourcePath(path);
    }
    throw std::runtime_error("Cannot save particle texture: its descriptor has no registered asset reference.");
}

Json CaptureParticle(RenderSystem& renderer, const ParticleSystem& emitter) {
    const auto& c = emitter.config;
    if (c.triggerControlled || c.scenePositionControlled)
        throw std::runtime_error("Native scene-controlled particle emitters require their original content module.");
    return {{"name", std::string(c.name)}, {"shape", static_cast<int>(emitter.getEmitterShape())}, {"count", emitter.count()},
        {"visible", c.isVisible}, {"useTexture", c.useTexture},
        {"texture", TexturePath(renderer, c.textureDescriptor, false)}, {"icon", TexturePath(renderer, c.uiIconDescriptor, true)},
        {"emitterPos", Vector(c.emitterPos)}, {"emitDir", Vector(c.emitDir)}, {"gravity", Vector(c.gravity)}, {"boxArea", Vector(c.boxArea)},
        {"lifeMin", c.lifeMin}, {"lifeMax", c.lifeMax}, {"speedMin", c.speedMin}, {"speedMax", c.speedMax},
        {"sizeMin", c.sizeMin}, {"sizeMax", c.sizeMax}, {"startColor", Vector(c.startColor)}, {"endColor", Vector(c.endColor)},
        {"coneSpread", c.coneSpread}, {"sphereRadius", c.sphereRadius}, {"atlasCols", c.atlasCols}, {"atlasRows", c.atlasRows},
        {"animateAtlas", c.animateAtlas}, {"startSizeScale", c.startSizeScale}, {"endSizeScale", c.endSizeScale},
        {"rotationMin", c.rotationMin}, {"rotationMax", c.rotationMax}, {"debug", c.particleDebug}};
}

void RestoreParticle(const Json& value, RenderSystem& renderer) {
    renderer.AddParticleGroup(value["count"].get<std::uint32_t>());
    auto& emitter = *renderer.GetParticles().back();
    auto& c = emitter.config;
    std::snprintf(c.name, sizeof(c.name), "%s", value["name"].get<std::string>().c_str());
    emitter.setEmitterShape(static_cast<EmitterShape>(value["shape"].get<int>()));
    c.isVisible = value["visible"].get<bool>();
    c.useTexture = value["useTexture"].get<int>();
    c.textureDescriptor = renderer.GetParticleTextureDescriptor(value["texture"].get<std::string>());
    c.uiIconDescriptor = renderer.GetImGuiTextureDescriptor(value["icon"].get<std::string>());
    c.emitterPos = ReadVec3(value["emitterPos"]);
    c.emitDir = ReadVec3(value["emitDir"]);
    c.gravity = ReadVec3(value["gravity"]);
    c.boxArea = ReadVec3(value["boxArea"]);
    c.lifeMin = value["lifeMin"].get<float>(); c.lifeMax = value["lifeMax"].get<float>();
    c.speedMin = value["speedMin"].get<float>(); c.speedMax = value["speedMax"].get<float>();
    c.sizeMin = value["sizeMin"].get<float>(); c.sizeMax = value["sizeMax"].get<float>();
    c.startColor = ReadVec4(value["startColor"]); c.endColor = ReadVec4(value["endColor"]);
    c.coneSpread = value["coneSpread"].get<float>(); c.sphereRadius = value["sphereRadius"].get<float>();
    c.atlasCols = value["atlasCols"].get<int>(); c.atlasRows = value["atlasRows"].get<int>();
    c.animateAtlas = value["animateAtlas"].get<bool>();
    c.startSizeScale = value["startSizeScale"].get<float>(); c.endSizeScale = value["endSizeScale"].get<float>();
    c.rotationMin = value["rotationMin"].get<float>(); c.rotationMax = value["rotationMax"].get<float>();
    c.particleDebug = value["debug"].get<bool>();
    c.triggerControlled = c.scenePositionControlled = false;
    // Reinitialize after authoring values are assigned so the first rendered
    // frame does not contain particles spawned from the default config.
    renderer.ResizeParticleGroup(renderer.GetParticles().size() - 1, value["count"].get<std::uint32_t>());
}

void RemoveBody(flecs::entity entity, PhysicsSystem* physics) {
    if (!entity.has<PhysicsBody>()) return;
    const JPH::BodyID id(entity.get<PhysicsBody>().bodyID);
    if (physics && physics->get_system()) {
        JPH::BodyLockRead lock(physics->get_system()->GetBodyLockInterface(), id);
        const bool exists = lock.Succeeded();
        lock.ReleaseLock();
        if (exists) {
            auto& bodyInterface = physics->get_body_interface();
            if (bodyInterface.IsAdded(id)) bodyInterface.RemoveBody(id);
            bodyInterface.DestroyBody(id);
        }
    }
    entity.remove<PhysicsBody>();
}

std::unordered_map<std::string, EngineModel> DecodeModels(const Json& document, const fs::path& assetRoot) {
    ValidateSceneDocumentAssets(document, assetRoot);
    std::unordered_map<std::string, EngineModel> decoded;
    for (const auto& value : document["models"]) {
        const auto source = fs::u8path(value["asset"].get<std::string>()).lexically_relative("Assets");
        const auto filename = (assetRoot / source).generic_u8string();
        const std::string utf8(reinterpret_cast<const char*>(filename.data()), filename.size());
        auto model = load_engine_model_glb(utf8.c_str());
        if (model.scenes.empty()) throw std::runtime_error("Model contains no renderable nodes: " + value["asset"].get<std::string>());
        for (const auto& part : model.scenes)
            if (part.meshIndex >= model.meshes.size() || model.meshes[part.meshIndex].materialIndex >= model.materials.size())
                throw std::runtime_error("Model contains an invalid mesh/material reference: " + value["asset"].get<std::string>());
        decoded.emplace(value["id"].get<std::string>(), std::move(model));
    }
    for (const auto& value : document["entities"]) if (value.contains("model")) {
        const auto& model = value["model"];
        if (model["part"].get<std::uint32_t>() >= decoded.at(model["instance"].get<std::string>()).scenes.size())
            throw std::runtime_error("Saved model node no longer exists: " + value["name"].get<std::string>());
    }
    return decoded;
}
} // namespace

void PreflightSceneDocumentResources(const Json& document, const fs::path& assetRoot) {
    (void)DecodeModels(document, assetRoot);
}

Json CaptureSceneDocument(SceneManager& scene, RenderSystem& renderer) {
    Json result{{"schemaVersion", 1}, {"models", Json::array()}, {"entities", Json::array()}, {"particles", Json::array()}};
    std::vector<flecs::entity> entities;
    scene.get_world().each([&](flecs::entity entity, const LocalTransform&) {
        if (std::string(entity.name().c_str() ? entity.name().c_str() : "") != "Preview_Object") entities.push_back(entity);
    });
    std::unordered_set<flecs::entity_t> captured;
    for (auto entity : entities) {
        captured.insert(entity.id());
        if (!entity.has<SceneEntityIdentity>()) entity.set<SceneEntityIdentity>({NewSceneIdentity()});
    }
    std::unordered_set<std::string> imported;
    std::unordered_map<std::string, const ModelImportSource*> sources;
    for (const auto& source : scene.ModelImports()) sources[source.id] = &source;
    for (auto entity : entities) {
        const std::string name = entity.name().c_str() ? entity.name().c_str() : "";
        if (entity.has<CompoundParent>() || entity.has<SkinComponent>() || entity.has<AnimationComponent>() ||
            entity.has<RiderBinding>() || entity.has<LODComponent>())
            throw std::runtime_error("Entity '" + name + "' has native animation/compound/LOD behavior; save it through its native project, not as a generic scene.");
        const auto parent = entity.parent();
        if (parent.is_valid() && parent.is_alive() && !captured.contains(parent.id()))
            throw std::runtime_error("Entity '" + name + "' has a parent without an authored transform.");
        const auto status = entity.has<EntityStatus>() ? entity.get<EntityStatus>() : EntityStatus{true, false};
        const bool isCamera = entity.has<GameCameraComponent>();
        Json value{{"id", entity.get<SceneEntityIdentity>().value}, {"name", name},
            {"parent", parent.is_valid() && parent.is_alive() ? Json(parent.get<SceneEntityIdentity>().value) : Json(nullptr)},
            {"transform", Matrix(isCamera ? entity.get<GameCameraComponent>().freeTransform : entity.get<LocalTransform>().matrix)},
            {"status", {{"visible", status.should_render}, {"physics", status.has_physics && entity.has<PhysicsBody>()}}},
            {"layer", entity.has<LayerComponent>() ? static_cast<int>(entity.get<LayerComponent>().layer) : 0}};
        if (entity.has<MeshComponent>()) {
            if (!entity.has<ModelPartSource>())
                throw std::runtime_error("Cannot save mesh '" + name + "': no source model resource. Reimport it from Assets or use its native project.");
            const auto& source = entity.get<ModelPartSource>();
            if (!sources.contains(source.instance)) throw std::runtime_error("Missing model import metadata for '" + name + "'.");
            if (!entity.has<MaterialComponent>() || entity.get<MeshComponent>().meshIndex != source.meshIndex ||
                entity.get<MaterialComponent>().materialIndex != source.materialIndex)
                throw std::runtime_error("Cannot save mesh/material override on '" + name + "' without an asset reference.");
            value["model"] = {{"instance", source.instance}, {"part", source.part}};
            imported.insert(source.instance);
        }
        else if (entity.has<PhysicsBody>()) throw std::runtime_error("Cannot save physics on '" + name + "' without a source model.");
        if (isCamera) {
            const auto& camera = entity.get<GameCameraComponent>();
            value["camera"] = {{"settings", SerializeCameraSettings(camera.settings)}, {"freeTransform", Matrix(camera.freeTransform)}};
        }
        if (entity.has<LightComponent>()) {
            value["light"] = SerializeLight(entity.get<LightComponent>());
        }
        result["entities"].push_back(std::move(value));
    }
    for (const auto& source : scene.ModelImports()) if (imported.contains(source.id)) {
        if (source.physicsType != static_cast<int>(ModelPhysicsType::Static) && source.physicsType != static_cast<int>(ModelPhysicsType::Dynamic))
            throw std::runtime_error("Compound vehicle model imports require their original native content module.");
        result["models"].push_back({{"id", source.id}, {"asset", ResourcePath(source.asset)},
            {"physicsType", source.physicsType}, {"mass", source.mass}, {"transform", Matrix(source.transform)}});
    }
    for (std::size_t index = 0; index < renderer.GetParticles().size(); ++index) {
        if (renderer.IsParticleGroupSceneControlled(index) || renderer.IsParticleGroupTriggerControlled(index))
            throw std::runtime_error("Native particle attachment/trigger behavior cannot be exported as a generic scene.");
        result["particles"].push_back(CaptureParticle(renderer, *renderer.GetParticles()[index]));
    }
    for (const auto* key : {"models", "entities"}) std::sort(result[key].begin(), result[key].end(), [](const Json& a, const Json& b) {
        return a["id"].get_ref<const std::string&>() < b["id"].get_ref<const std::string&>();
    });
    ValidateSceneDocumentAssets(result, fs::path("Assets"));
    return result;
}

void InstantiateSceneDocument(const Json& document, SceneManager& scene, RenderSystem& renderer) {
    ValidateSceneDocumentAssets(document, fs::path("Assets"));
    auto& world = scene.get_world();
    bool occupied = false;
    world.each([&](flecs::entity entity, const LocalTransform&) {
        if (!entity.has<GameCameraComponent>() || entity != FindGameCameraEntity(world)) occupied = true;
    });
    if (occupied || !renderer.GetParticles().empty())
        throw std::runtime_error("Scene document requires a fresh world (only the default game camera may already exist).");

    // Decode every source and verify saved part references before changing the
    // world. GPU allocation failures are handled by the caller's session rollback.
    auto decoded = DecodeModels(document, fs::path("Assets"));
    std::unordered_map<std::string, const Json*> modelRecords;
    for (const auto& value : document["models"]) {
        const auto id = value["id"].get<std::string>();
        modelRecords[id] = &value;
    }
    for (const auto& value : document["particles"]) {
        const auto texture = value["texture"].get<std::string>();
        const auto icon = value["icon"].get<std::string>();
        if ((!texture.empty() && renderer.GetParticleTextureDescriptor(texture) == VK_NULL_HANDLE) ||
            (!icon.empty() && renderer.GetImGuiTextureDescriptor(icon) == VK_NULL_HANDLE))
            throw std::runtime_error("Particle resource is not registered by the renderer: " + (texture.empty() ? icon : texture));
    }

    std::map<std::pair<std::string, std::uint32_t>, flecs::entity> imported;
    for (const auto& value : document["models"]) {
        const auto id = value["id"].get<std::string>();
        const auto path = value["asset"].get<std::string>();
        scene.LoadModelData(&renderer, path.c_str(), std::move(decoded.at(id)),
            static_cast<ModelPhysicsType>(value["physicsType"].get<int>()), value["mass"].get<float>(),
            ReadMatrix(value["transform"]), RenderLayer::Default);
        scene.SetModelImportIdentity(scene.ModelImports().back().id, id);
    }
    world.each([&](flecs::entity entity, const ModelPartSource& source) {
        imported.emplace(std::pair{source.instance, source.part}, entity);
    });
    std::unordered_map<std::string, flecs::entity> entities;
    std::unordered_set<flecs::entity_t> retained;
    for (const auto& value : document["entities"]) {
        flecs::entity entity = flecs::entity::null();
        if (value.contains("model")) {
            const auto& model = value["model"];
            entity = imported.at({model["instance"].get<std::string>(), model["part"].get<std::uint32_t>()});
        }
        else if (value.contains("camera")) entity = FindGameCameraEntity(world);
        if (!entity.is_valid() || !entity.is_alive()) entity = world.entity();
        retained.insert(entity.id());
        entities.emplace(value["id"].get<std::string>(), entity);
    }
    for (const auto& [key, entity] : imported) if (!retained.contains(entity.id())) scene.DestroyEntity(entity);
    // Imported names may collide with restored names. Clear all first; then set
    // parent relations before restoring sibling names (same names in two groups are valid).
    for (const auto& [id, entity] : entities) entity.set_name(nullptr);
    for (const auto& value : document["entities"]) {
        auto entity = entities.at(value["id"].get<std::string>());
        if (!value["parent"].is_null()) entity.child_of(entities.at(value["parent"].get<std::string>()));
        const auto name = value["name"].get<std::string>();
        if (!name.empty()) entity.set_name(name.c_str());
        entity.set<SceneEntityIdentity>({value["id"].get<std::string>()});
        entity.set<LocalTransform>({ReadMatrix(value["transform"])});
        entity.set<EntityStatus>({value["status"]["visible"].get<bool>(), value["status"]["physics"].get<bool>()});
        entity.set<LayerComponent>({static_cast<RenderLayer>(value["layer"].get<int>())});
        if (value.contains("camera")) entity.set<GameCameraComponent>({
            DeserializeCameraSettings(value["camera"]["settings"]), ReadMatrix(value["camera"]["freeTransform"]), true});
        if (value.contains("light")) {
            entity.set<LightComponent>(DeserializeLight(value["light"]));
        }
    }
    std::unordered_set<flecs::entity_t> resolved;
    std::function<glm::mat4(flecs::entity)> worldTransform = [&](flecs::entity entity) {
        if (resolved.contains(entity.id())) return entity.get<WorldTransform>().matrix;
        glm::mat4 transform = entity.get<LocalTransform>().matrix;
        auto parent = entity.parent();
        if (parent.is_valid() && parent.is_alive()) transform = worldTransform(parent) * transform;
        entity.set<WorldTransform>({transform});
        resolved.insert(entity.id());
        return transform;
    };
    auto* physics = scene.get_physics_system();
    for (const auto& value : document["entities"]) {
        auto entity = entities.at(value["id"].get<std::string>());
        const auto transform = worldTransform(entity);
        RemoveBody(entity, physics);
        if (!value.contains("model") || !value["status"]["physics"].get<bool>() || !physics) continue;
        const auto& source = *modelRecords.at(value["model"]["instance"].get<std::string>());
        const auto& mesh = scene.get_model().meshes.at(entity.get<MeshComponent>().meshIndex);
        const auto id = source["physicsType"].get<int>() == static_cast<int>(ModelPhysicsType::Static)
            ? physics->create_static_mesh_body(mesh, transform)
            : physics->create_dynamic_convex_body(mesh, transform, source["mass"].get<float>());
        if (id.IsInvalid()) throw std::runtime_error("Could not reconstruct physics for '" + value["name"].get<std::string>() + "'.");
        entity.set<PhysicsBody>({id.GetIndexAndSequenceNumber()});
    }
    for (const auto& value : document["particles"]) RestoreParticle(value, renderer);
    if (physics) physics->optimize_broad_phase();
}
} // namespace engine
