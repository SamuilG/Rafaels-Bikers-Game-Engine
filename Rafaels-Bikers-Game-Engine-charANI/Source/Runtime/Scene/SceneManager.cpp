#include "SceneManager.hpp"
#include <flecs.h>
#include <print>
#include <cmath>

#include "../Physics/PhysicsSystem.hpp"
#include "../Renderer/RenderSystem.hpp"
#include "../Renderer/RenderUtilities/light.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtx/matrix_decompose.hpp> 

// =========================================================================
// 全局辅助函数
// =========================================================================
EngineMesh generate_uv_sphere(float radius, uint32_t rings, uint32_t sectors)
{
    EngineMesh mesh;
    const float pi = glm::pi<float>();

    for (uint32_t r = 0; r <= rings; ++r) {
        float const phi = pi * r / rings; // 0 .. pi
        for (uint32_t s = 0; s <= sectors; ++s) {
            float const theta = 2.f * pi * s / sectors; // 0 .. 2pi

            float x = std::sin(phi) * std::cos(theta);
            float y = std::cos(phi);
            float z = std::sin(phi) * std::sin(theta);

            mesh.positions.emplace_back(radius * x, radius * y, radius * z);
            mesh.normals.emplace_back(x, y, z);
            mesh.texcoords.emplace_back(
                static_cast<float>(s) / sectors,
                static_cast<float>(r) / rings);
        }
    }

    for (uint32_t r = 0; r < rings; ++r) {
        for (uint32_t s = 0; s < sectors; ++s) {
            uint32_t a = r * (sectors + 1) + s;
            uint32_t b = (r + 1) * (sectors + 1) + s;
            mesh.indices.push_back(a);     mesh.indices.push_back(b);     mesh.indices.push_back(a + 1);
            mesh.indices.push_back(b);     mesh.indices.push_back(b + 1); mesh.indices.push_back(a + 1);
        }
    }
    mesh.localAabbMin = glm::vec3(-radius);
    mesh.localAabbMax = glm::vec3(radius);

    return mesh;
}

namespace engine {

    SceneManager::SceneManager(PhysicsSystem* physics_system)
        : m_physics_system(physics_system) {
    }

    SceneManager::~SceneManager() {
        Shutdown();
    }

    void SceneManager::Init() {
        m_world = new flecs::world();

        // Robust hierarchical transform system
        m_world->system<WorldTransform, const LocalTransform>("UpdateWorldTransform")
            .kind(flecs::OnUpdate)
            .each([](flecs::entity e, WorldTransform& wt, const LocalTransform& lt) {
            auto parent = e.parent();

            if (parent.is_valid() && parent.has<WorldTransform>()) {
                const WorldTransform* pwt = &parent.get<WorldTransform>();
                wt.matrix = pwt->matrix * lt.matrix;
            }
            else {
                wt.matrix = lt.matrix;
            }
                });
        speed = 0.0f;
    }

    void SceneManager::Shutdown() {
        if (m_world) {
            delete m_world;
            m_world = nullptr;
        }
    }

    void SceneManager::cache_model_for_culling(const EngineModel& model, uint32_t baseMeshIdx, uint32_t baseMatIdx) {
        if (mModel.materials.size() < baseMatIdx) {
            mModel.materials.resize(baseMatIdx);
        }
        if (mModel.meshes.size() < baseMeshIdx) {
            mModel.meshes.resize(baseMeshIdx);
        }

        for (const auto& material : model.materials) {
            mModel.materials.push_back(material);
        }

        for (auto mesh : model.meshes) {
            mesh.materialIndex += baseMatIdx;
            mModel.meshes.push_back(std::move(mesh));
        }

        for (auto instance : model.scenes) {
            instance.meshIndex += baseMeshIdx;
            mModel.scenes.push_back(std::move(instance));
        }
    }

    // =========================================================================
    // 模型加载 API
    // =========================================================================

    flecs::entity SceneManager::LoadModel(engine::RenderSystem* renderSystem, const char* path, ModelPhysicsType physicsType, float mass, const glm::mat4& initialTransform, RenderLayer layer)
    {
        EngineModel newModel = load_engine_model_glb(path);

        for (auto& instance : newModel.scenes) {
            instance.transform = initialTransform * instance.transform;
        }

        auto offsets = renderSystem->RegisterModelAssets(newModel);

        switch (physicsType) {
        case ModelPhysicsType::Compound:
            return load_compound_model(newModel, mass, offsets.baseMeshIdx, offsets.baseMaterialIdx, layer);
        case ModelPhysicsType::CustomC:
            return load_C_model(newModel, mass, offsets.baseMeshIdx, offsets.baseMaterialIdx, layer);
        case ModelPhysicsType::Static:
            return load_static_model(newModel, offsets.baseMeshIdx, offsets.baseMaterialIdx, layer);
        case ModelPhysicsType::Dynamic:
            return load_dynamic_model(newModel, mass, offsets.baseMeshIdx, offsets.baseMaterialIdx, layer);
        }
        return flecs::entity::null();
    }

    flecs::entity SceneManager::load_static_model(const EngineModel& model, uint32_t baseMeshIdx, uint32_t baseMatIdx, RenderLayer layer) {
        std::print("Loading static model with {} instances\n", model.scenes.size());
        cache_model_for_culling(model, baseMeshIdx, baseMatIdx);
        flecs::entity firstEntity = flecs::entity::null();

        int counter = 0;
        for (const auto& instance : model.scenes) {
            std::string name = instance.name.empty() ? "StaticObject" : instance.name;
            name += "_" + std::to_string(counter++);

            auto e = m_world->entity(name.c_str())
                .add<StaticObject>()
                .set<EntityStatus>({ true, true })
                .set<LayerComponent>({ layer })
                .set<LocalTransform>({ instance.transform })
                .set<WorldTransform>({ instance.transform })
                .set<MeshComponent>({ instance.meshIndex + baseMeshIdx });

            uint32_t matIdx = model.meshes[instance.meshIndex].materialIndex + baseMatIdx;
            e.set<MaterialComponent>({ matIdx });

            if (m_physics_system) {
                JPH::BodyID bodyID = m_physics_system->create_static_mesh_body(model.meshes[instance.meshIndex], instance.transform);
                if (!bodyID.IsInvalid()) {
                    e.set<PhysicsBody>({ bodyID.GetIndexAndSequenceNumber() });
                }
            }

            if (!firstEntity.is_valid()) {
                firstEntity = e;
            }
        }
        return firstEntity;
    }

    flecs::entity SceneManager::load_dynamic_model(const EngineModel& model, float mass, uint32_t baseMeshIdx, uint32_t baseMatIdx, RenderLayer layer) {
        cache_model_for_culling(model, baseMeshIdx, baseMatIdx);
        flecs::entity firstEntity = flecs::entity::null();

        int counter = 0;
        for (const auto& instance : model.scenes) {
            std::string name = instance.name.empty() ? "DynamicObject" : instance.name;
            name += "_" + std::to_string(counter++);

            auto e = m_world->entity(name.c_str())
                .add<DynamicObject>()
                .set<EntityStatus>({ true, true })
                .set<LayerComponent>({ layer })
                .set<LocalTransform>({ instance.transform })
                .set<WorldTransform>({ instance.transform })
                .set<MeshComponent>({ instance.meshIndex + baseMeshIdx });

            uint32_t matIdx = model.meshes[instance.meshIndex].materialIndex + baseMatIdx;
            e.set<MaterialComponent>({ matIdx });

            if (m_physics_system) {
                JPH::BodyID bodyID = m_physics_system->create_dynamic_convex_body(model.meshes[instance.meshIndex], instance.transform, mass);
                if (!bodyID.IsInvalid()) {
                    e.set<PhysicsBody>({ bodyID.GetIndexAndSequenceNumber() });
                }
            }

            if (!firstEntity.is_valid()) {
                firstEntity = e;
            }
        }
        return firstEntity;
    }

    flecs::entity SceneManager::load_compound_model(const EngineModel& model, float mass, uint32_t baseMeshIdx, uint32_t baseMatIdx, RenderLayer layer) {
        std::print("Loading compound model with {} instances\n", model.scenes.size());
        cache_model_for_culling(model, baseMeshIdx, baseMatIdx);

        flecs::entity firstEntity = flecs::entity::null();
        JPH::BodyID compoundBodyID;
        glm::mat4 bodyWorldTransform = model.scenes.empty() ? glm::mat4(1.0f) : model.scenes[0].transform;

        if (m_physics_system && !model.scenes.empty()) {
            std::vector<EngineMesh> meshes;
            std::vector<glm::mat4> meshTransforms;
            for (const auto& instance : model.scenes) {
                meshes.push_back(model.meshes[instance.meshIndex]);
                meshTransforms.push_back(instance.transform);
            }
            compoundBodyID = m_physics_system->create_dynamic_compound_body(
                meshes, meshTransforms, bodyWorldTransform, mass);
        }

        glm::mat4 invBody = glm::inverse(bodyWorldTransform);

        int counter = 0;
        for (const auto& instance : model.scenes) {
            std::string name = instance.name.empty() ? "CompoundPart" : instance.name;
            name += "_" + std::to_string(counter++);

            glm::mat4 localOffset = invBody * instance.transform;

            auto e = m_world->entity(name.c_str())
                .add<DynamicObject>()
                .set<EntityStatus>({ true, false })
                .set<LayerComponent>({ layer })
                .set<LocalTransform>({ instance.transform })
                .set<WorldTransform>({ instance.transform })
                .set<MeshComponent>({ instance.meshIndex + baseMeshIdx })
                .set<CompoundParent>({ compoundBodyID.GetIndexAndSequenceNumber(), localOffset });

            uint32_t matIdx = model.meshes[instance.meshIndex].materialIndex + baseMatIdx;
            e.set<MaterialComponent>({ matIdx });

            if (!firstEntity.is_valid()) {
                firstEntity = e;
            }
        }
        return firstEntity;
    }

    flecs::entity SceneManager::load_C_model(const EngineModel& model, float mass, uint32_t baseMeshIdx, uint32_t baseMatIdx, RenderLayer layer) {
        std::print("Loading C model with {} instances\n", model.scenes.size());
        flecs::entity firstEntity = flecs::entity::null();
        cache_model_for_culling(model, baseMeshIdx, baseMatIdx);

        glm::mat4 bodyWorldTransform = glm::mat4(1.0f);

        if (!model.scenes.empty()) {
            glm::vec3 scale, translation, skew;
            glm::quat rotation;
            glm::vec4 perspective;

            glm::decompose(model.scenes[0].transform, scale, rotation, translation, skew, perspective);
            rotation = glm::normalize(rotation);

            bodyWorldTransform = glm::translate(glm::mat4(1.0f), translation) * glm::mat4_cast(rotation);
        }

        JPH::BodyID compoundBodyID;
        if (m_physics_system && !model.scenes.empty()) {
            std::vector<EngineMesh> meshes;
            std::vector<glm::mat4> meshTransforms;
            for (const auto& instance : model.scenes) {
                meshes.push_back(model.meshes[instance.meshIndex]);
                meshTransforms.push_back(instance.transform);
            }
            compoundBodyID = m_physics_system->create_dynamic_compound_body(
                meshes, meshTransforms, bodyWorldTransform, mass);
        }

        glm::mat4 invBody = glm::inverse(bodyWorldTransform);

        int counter = 0;
        for (const auto& instance : model.scenes) {
            std::string name = instance.name.empty() ? "CPart" : instance.name;
            name += "_" + std::to_string(counter++);

            glm::mat4 localOffset = invBody * instance.transform;

            auto e = m_world->entity(name.c_str())
                .add<DynamicObject>()
                .set<EntityStatus>({ true, true })
                .set<LayerComponent>({ layer })
                .set<LocalTransform>({ instance.transform })
                .set<WorldTransform>({ instance.transform })
                .set<MeshComponent>({ instance.meshIndex + baseMeshIdx })
                .set<CompoundParent>({ compoundBodyID.GetIndexAndSequenceNumber(), localOffset });

            uint32_t matIdx = model.meshes[instance.meshIndex].materialIndex + baseMatIdx;
            e.set<MaterialComponent>({ matIdx });

            if (!firstEntity.is_valid()) {
                firstEntity = e;
            }
        }
        return firstEntity;
    }


    // =========================================================================
    // 实体与系统工具 API
    // =========================================================================

    flecs::entity SceneManager::find_entity(const char* name) {
        return m_world->lookup(name);
    }

    void SceneManager::set_local_transform(flecs::entity e, const glm::mat4& transform) {
        if (e.is_valid()) {
            e.set<LocalTransform>({ transform });
        }
    }

    std::string SceneManager::get_entity_name_from_body_id(uint32_t bodyID) {
        std::string result = "Unknown Entity";
        m_world->query<const PhysicsBody>()
            .each([&](flecs::entity e, const PhysicsBody& pb) {
            if (pb.bodyID == bodyID) {
                result = e.name() ? e.name() : "unnamed";
            }
                });
        return result;
    }

    flecs::entity SceneManager::create_dynamic_entity(
        const char* name, uint32_t meshIndex, uint32_t matIndex,
        const glm::mat4& transform, uint32_t physicsBodyID)
    {
        auto e = m_world->entity(name)
            .add<DynamicObject>()
            .set<EntityStatus>({ true, true })
            .set<LocalTransform>({ transform })
            .set<WorldTransform>({ transform })
            .set<MeshComponent>({ meshIndex })
            .set<MaterialComponent>({ matIndex });

        if (physicsBodyID != ~0u) {
            e.set<PhysicsBody>({ physicsBodyID });
        }

        return e;
    }

    flecs::entity SceneManager::raycast_entity(const glm::vec3& origin, const glm::vec3& direction, float max_distance)
    {
        if (!m_physics_system) return flecs::entity::null();

        uint32_t hitBodyID = m_physics_system->cast_ray(origin, direction, max_distance);

        if (hitBodyID == JPH::BodyID::cInvalidBodyID) {
            return flecs::entity::null();
        }
        m_physics_system->get_body_interface().ActivateBody(JPH::BodyID(hitBodyID));

        flecs::entity hitEntity = flecs::entity::null();
        m_world->query<const PhysicsBody>().each([&](flecs::entity e, const PhysicsBody& pb) {
            if (pb.bodyID == hitBodyID) {
                hitEntity = e;
            }
            });

        return hitEntity;
    }


    // =========================================================================
    // 灯光与渲染数据提取
    // =========================================================================

    flecs::entity SceneManager::create_light_entity(
        const char* name, LightType type, glm::vec3 color, float intensity,
        const glm::mat4& transform, float range, glm::vec3 direction,
        float innerCutOff, float outerCutOff, flecs::entity parent)
    {
        auto e = m_world->entity(name)
            .set<EntityStatus>({ true, false })
            .set<LocalTransform>({ transform })
            .set<WorldTransform>({ transform })
            .set<LightComponent>({ color, intensity, type, range, direction, innerCutOff, outerCutOff });

        if (parent.is_valid()) {
            e.child_of(parent);
        }

        return e;
    }

    void SceneManager::get_light_data(std::vector<GpuLight>& outLights) {
        outLights.clear();

        m_world->query<const WorldTransform, const LightComponent, const EntityStatus>()
            .each([&](const WorldTransform& wt, const LightComponent& lc, const EntityStatus& status) {
            if (!status.should_render) return;
            if (outLights.size() >= 16) return;

            GpuLight gpuLight{};
            gpuLight.position = glm::vec4(glm::vec3(wt.matrix[3]), static_cast<float>(lc.type));
            gpuLight.color = glm::vec4(lc.color, lc.intensity);

            glm::vec3 worldDir = glm::normalize(glm::vec3(wt.matrix * glm::vec4(lc.direction, 0.0f)));
            gpuLight.direction = glm::vec4(worldDir, lc.range);

            float cosInner = glm::cos(glm::radians(lc.innerCutOff));
            float cosOuter = glm::cos(glm::radians(lc.outerCutOff));
            gpuLight.params = glm::vec4(cosInner, cosOuter, 0.0f, 0.0f);

            outLights.push_back(gpuLight);
                });
    }

    std::vector<RenderBatch> SceneManager::get_render_batches(const Frustum* frustum, float frustumPadding) {
        std::vector<RenderBatch> batches;
        mLastFrustumCullingCandidates = 0;
        mLastFrustumCullingVisible = 0;

        m_world->query<const WorldTransform, const MeshComponent, const MaterialComponent, const EntityStatus, const OpacityComponent*>()
            .each([&](flecs::entity e, const WorldTransform& wt, const MeshComponent& mc, const MaterialComponent& matc, const EntityStatus& status, const OpacityComponent* op) {

            if (!status.should_render) return;
            ++mLastFrustumCullingCandidates;

            if (frustum && mc.meshIndex < mModel.meshes.size()) {
                const EngineMesh& mesh = mModel.meshes[mc.meshIndex];

                glm::vec3 worldAabbMin(0.0f);
                glm::vec3 worldAabbMax(0.0f);
                TransformAabb(mesh.localAabbMin, mesh.localAabbMax, wt.matrix, worldAabbMin, worldAabbMax);

                if (!IntersectsAabb(*frustum, worldAabbMin, worldAabbMax, frustumPadding)) {
                    return;
                }
            }

            ++mLastFrustumCullingVisible;

            float alpha = op ? op->currentAlpha : 1.0f;

            // 检查实体的渲染层决定是否投射阴影
            bool castsShadow = true;
            if (e.has<LayerComponent>()) {
                if (e.get<LayerComponent>().layer == RenderLayer::Emissive) {
                    castsShadow = false;
                }
            }

            batches.push_back({ mc.meshIndex, matc.materialIndex, wt.matrix, alpha, castsShadow });
                });
        return batches;
    }


    // =========================================================================
    // 每帧更新 (Physics Sync, Animations, etc)
    // =========================================================================

    void SceneManager::Update(float dt) {
        if (m_physics_system) {
            JPH::BodyInterface& bodyInterface = m_physics_system->get_body_interface();

            // 1. 同步普通刚体位置
            m_world->query<LocalTransform, const PhysicsBody, const EntityStatus>()
                .each([&](flecs::entity e, LocalTransform& lt, const PhysicsBody& pb, const EntityStatus& status) {
                if (!status.has_physics) return;
                JPH::BodyID bodyID(pb.bodyID);

                if (bodyInterface.IsActive(bodyID)) {
                    JPH::Vec3 pos = bodyInterface.GetPosition(bodyID);
                    JPH::Quat rot = bodyInterface.GetRotation(bodyID);

                    glm::mat4 translation = glm::translate(glm::mat4(1.0f), toGlm(pos));
                    glm::mat4 rotation = glm::mat4_cast(toGlm(rot));

                    glm::vec3 scale(
                        glm::length(glm::vec3(lt.matrix[0])),
                        glm::length(glm::vec3(lt.matrix[1])),
                        glm::length(glm::vec3(lt.matrix[2]))
                    );

                    lt.matrix = translation * rotation * glm::scale(glm::mat4(1.0f), scale);
                }
                    });

            // 2. 同步复杂车辆动画逻辑
            m_world->query<LocalTransform, const CompoundParent>()
                .each([&](flecs::entity e, LocalTransform& lt, const CompoundParent& cp) {
                JPH::BodyID bodyID(cp.bodyID);
                if (bodyInterface.IsActive(bodyID)) {
                    JPH::Vec3 pos = bodyInterface.GetPosition(bodyID);
                    JPH::Quat rot = bodyInterface.GetRotation(bodyID);

                    glm::mat4 bodyWorld = glm::translate(glm::mat4(1.0f), toGlm(pos))
                        * glm::mat4_cast(toGlm(rot));

                    glm::mat4 finalOffset = cp.localOffset;
                    const char* name = e.name();

                    if (name != nullptr) {
                        if (strstr(name, "steer.001_2") || strstr(name, "frontwheel.001_3")) {
                            float steerAngle = mState ? mState->bikeSteerAngle : 0.0f;
                            glm::mat4 steerRot = glm::rotate(glm::mat4(1.0f), steerAngle, glm::vec3(0.0f, 1.0f, 0.0f));
                            finalOffset = finalOffset * steerRot;
                        }

                        if (strstr(name, "frontwheel.001_3") || strstr(name, "Rear wheel_1") || strstr(name, "Pedal.002_5") || strstr(name, "WholePedal_4")) {
                            float currentSpeed = mState ? mState->bikeSpeed : 0.0f;
                            speed += 0.001f * currentSpeed;
                            glm::mat4 selfRot = glm::rotate(glm::mat4(1.0f), speed, glm::vec3(1.0f, 0.0f, 0.0f));
                            finalOffset = finalOffset * selfRot;
                        }
                    }
                    lt.matrix = bodyWorld * finalOffset;
                }
                    });

            // 3. 第三人称相机遮挡透视 (X-Ray) 逻辑
            if (mState && mState->thirdPersonMode) {
                glm::vec3 cameraPos = glm::vec3(mState->camera2world[3]);

                m_world->defer_begin();
                m_world->query<OpacityComponent>().each([&](flecs::entity e, OpacityComponent& op) {
                    op.currentAlpha += (op.targetAlpha - op.currentAlpha) * 8.0f * dt;
                    op.targetAlpha = 1.0f;
                    if (op.currentAlpha >= 0.99f && op.targetAlpha == 1.0f) {
                        e.remove<OpacityComponent>();
                    }
                    });
                m_world->defer_end();

                flecs::entity bikeEntity = find_entity("Bike_0");
                if (bikeEntity.is_valid() && bikeEntity.has<WorldTransform>()) {
                    uint32_t bikeBodyID = JPH::BodyID::cInvalidBodyID;
                    if (bikeEntity.has<CompoundParent>()) {
                        bikeBodyID = bikeEntity.get<CompoundParent>().bodyID;
                    }
                    else if (bikeEntity.has<PhysicsBody>()) {
                        bikeBodyID = bikeEntity.get<PhysicsBody>().bodyID;
                    }

                    if (bikeBodyID != JPH::BodyID::cInvalidBodyID) {
                        glm::vec3 bikePos = glm::vec3(bikeEntity.get<WorldTransform>().matrix[3]);
                        bikePos.y += 0.8f;

                        std::vector<uint32_t> ignoredIDs;
                        ignoredIDs.push_back(bikeBodyID);

                        m_world->defer_begin();
                        while (true) {
                            uint32_t hitBodyID = m_physics_system->cast_ray_ignore_multiple(cameraPos, bikePos, ignoredIDs);

                            if (hitBodyID == JPH::BodyID::cInvalidBodyID) {
                                break;
                            }

                            // std::print("[X-Ray] Hit blocking object ID: {}\n", hitBodyID);

                            m_world->query<const PhysicsBody>().each([&](flecs::entity e, const PhysicsBody& pb) {
                                if (pb.bodyID == hitBodyID) {
                                    if (!e.has<OpacityComponent>()) e.set<OpacityComponent>({ 1.0f, 0.3f });
                                    else e.get_mut<OpacityComponent>().targetAlpha = 0.3f;
                                }
                                });

                            m_world->query<const CompoundParent>().each([&](flecs::entity e, const CompoundParent& cp) {
                                if (cp.bodyID == hitBodyID) {
                                    if (!e.has<OpacityComponent>()) e.set<OpacityComponent>({ 1.0f, 0.3f });
                                    else e.get_mut<OpacityComponent>().targetAlpha = 0.3f;
                                }
                                });

                            ignoredIDs.push_back(hitBodyID);
                        }
                        m_world->defer_end();
                    }
                }
            }
        }
        m_world->progress(dt);
    }

    void SceneManager::print_all_entities() {
        std::print("\n--- [Entity List] ---\n");

        m_world->each([&](flecs::entity e, EntityStatus& status) {
            const char* name = e.name() ? e.name() : "unnamed";
            std::string typeStr = "Unknown";

            if (e.has<MeshComponent>()) {
                typeStr = "Mesh [" + std::to_string(e.get<MeshComponent>().meshIndex) + "]";
            }
            else if (e.has<LightComponent>()) {
                auto& lc = e.get<LightComponent>();
                if (lc.type == LightType::Directional) typeStr = "DirLight";
                else if (lc.type == LightType::Spot) typeStr = "SpotLight";
                else typeStr = "PointLight";
            }

            std::string bodyIdStr = "None";
            if (e.has<PhysicsBody>()) {
                bodyIdStr = std::to_string(e.get<PhysicsBody>().bodyID);
            }
            else if (e.has<CompoundParent>()) {
                bodyIdStr = std::to_string(e.get<CompoundParent>().bodyID) + " (Sub)";
            }

            std::print("Entity: {:<18} | Type: {:<12} | Render: {} | Physics: {} | BodyID: {}\n",
                name, typeStr,
                status.should_render ? "ON" : "OFF",
                status.has_physics ? "ON" : "OFF",
                bodyIdStr);
            });
        std::print("--------------------------------------\n\n");
    }

} // namespace engine