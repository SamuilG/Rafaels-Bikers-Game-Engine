#include "SceneManager.hpp"
#include "SceneFrameUpdate.hpp"
#include "SceneHierarchy.hpp"
#include <flecs.h>
#include <print>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <functional>
#include <unordered_map>
#include <unordered_set>

#include "../Physics/PhysicsSystem.hpp"
#include "../Physics/PhysicsTransform.hpp"
#include "../Animation/AnimationSystem.hpp" // for RiderIKComponent + IKChainConfig
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

    namespace {
        glm::mat4 AuthoredWorldTransform(flecs::entity entity) {
            return scene_hierarchy::World(entity);
        }
        std::string Utf8Path(const std::filesystem::path& path) {
            const auto value = path.generic_u8string();
            return {reinterpret_cast<const char*>(value.data()), value.size()};
        }
    }

    SceneManager::SceneManager(PhysicsSystem* physics_system)
        : m_physics_system(physics_system) {
    }

    SceneManager::~SceneManager() {
        Shutdown();
    }

    void SceneManager::Init() 
    {
        mModel = {};
        mModelImports.clear();
        mLoadingImport = nullptr;
        mLoadingAsset.reset();
        mStaticEntityCounter = mDynamicEntityCounter = mCompoundEntityCounter = 0;
        mCustomEntityCounter = mAnimatedEntityCounter = 0;
        m_world = new flecs::world();

        m_world->component<LODComponent>();

        // Robust hierarchical transform system
        mTransformSystem = m_world->system<WorldTransform, const LocalTransform>("UpdateWorldTransform")
            .kind(flecs::OnUpdate)
            .each([](flecs::entity e, WorldTransform& wt, const LocalTransform& lt) {
            // Resolve from authored local transforms, independent of Flecs table
            // iteration order when a parent and child occupy different archetypes.
            wt.matrix = AuthoredWorldTransform(e);
                });
        speed = 0.0f;
    }

    void SceneManager::Shutdown() {
        mTransformSystem = flecs::system{};
        if (m_world) {
            delete m_world;
            m_world = nullptr;
        }
        mModel = {};
        mModelImports.clear();
        mLoadingImport = nullptr;
    }

    void SceneManager::cache_model_for_culling(const EngineModel& model, uint32_t baseMeshIdx, uint32_t baseMatIdx) {
        mModel.materials.resize(std::max(mModel.materials.size(), std::size_t(baseMatIdx) + model.materials.size()));
        mModel.meshes.resize(std::max(mModel.meshes.size(), std::size_t(baseMeshIdx) + model.meshes.size()));
        for (std::size_t i = 0; i < model.materials.size(); ++i) mModel.materials[baseMatIdx + i] = model.materials[i];
        for (std::size_t i = 0; i < model.meshes.size(); ++i) {
            mModel.meshes[baseMeshIdx + i] = model.meshes[i];
            mModel.meshes[baseMeshIdx + i].materialIndex += baseMatIdx;
        }
    }

    // =========================================================================
    // 模型加载 API
    // =========================================================================

    flecs::entity SceneManager::LoadModel(engine::RenderSystem* renderSystem, const char* path, ModelPhysicsType physicsType, float mass, const glm::mat4& initialTransform, RenderLayer layer)
    {
        return InstantiateModelAsset(renderSystem, path, renderSystem->AcquireModelAsset(path), physicsType, mass, initialTransform, layer);
    }

    flecs::entity SceneManager::LoadModelData(engine::RenderSystem* renderSystem, const char* path,
        EngineModel newModel, ModelPhysicsType physicsType, float mass, const glm::mat4& initialTransform, RenderLayer layer)
    {

        return InstantiateModelAsset(renderSystem, path, renderSystem->AcquireModelAsset(path, &newModel),
            physicsType, mass, initialTransform, layer);
    }

    flecs::entity SceneManager::InstantiateModelAsset(RenderSystem* renderSystem, const char* path,
        std::shared_ptr<const ModelAsset> asset, ModelPhysicsType physicsType, float mass,
        const glm::mat4& initialTransform, RenderLayer layer) {
        EngineModel newModel = asset->model;
        for (auto& instance : newModel.scenes) instance.transform = initialTransform * instance.transform;
        const auto baseMeshIdx = asset->firstMesh;
        const auto baseMaterialIdx = asset->firstMaterial;
        mLoadingAsset = std::move(asset);
        mModelImports.push_back({ NewSceneIdentity(),
            Utf8Path(std::filesystem::absolute(std::filesystem::u8path(path)).lexically_normal()),
            static_cast<int>(physicsType), mass, initialTransform });
        mLoadingImport = &mModelImports.back();
        mLoadingPart = 0;
        flecs::entity result = flecs::entity::null();
        try {
            switch (physicsType) {
            case ModelPhysicsType::Compound:
                result = load_compound_model(newModel, mass, baseMeshIdx, baseMaterialIdx, layer); break;
            case ModelPhysicsType::CustomC:
                result = load_C_model(newModel, mass, baseMeshIdx, baseMaterialIdx, layer); break;
            case ModelPhysicsType::Static:
                result = load_static_model(newModel, baseMeshIdx, baseMaterialIdx, layer); break;
            case ModelPhysicsType::Dynamic:
                result = load_dynamic_model(newModel, mass, baseMeshIdx, baseMaterialIdx, layer); break;
            }
        }
        catch (...) {
            mLoadingImport = nullptr;
            mLoadingAsset.reset();
            throw;
        }
        mLoadingImport = nullptr;
        mLoadingAsset.reset();
        return result;
    }

    void SceneManager::RecordModelPart(flecs::entity entity) {
        if (mLoadingAsset) entity.set<ModelAssetReference>({mLoadingAsset});
        if (!mLoadingImport) return; // Native procedural import paths are not scene assets.
        entity.set<SceneEntityIdentity>({ NewSceneIdentity() });
        entity.set<ModelPartSource>({ mLoadingImport->id, mLoadingPart++,
            entity.get<MeshComponent>().meshIndex, entity.get<MaterialComponent>().materialIndex });
    }

    void SceneManager::SetModelImportIdentity(std::string previous, const std::string& restored) {
        for (auto& source : mModelImports) if (source.id == previous) source.id = restored;
        m_world->each([&](flecs::entity entity, ModelPartSource& source) {
            if (source.instance == previous) { source.instance = restored; entity.modified<ModelPartSource>(); }
        });
    }

    void SceneManager::DestroyEntity(flecs::entity entity) {
        if (!m_world || !entity.is_valid() || !entity.is_alive()) return;
        // Batch hierarchy deletion: scan remaining leases/imports once, rather
        // than once per primitive of a large imported model.
        std::unordered_map<const ModelAsset*, std::shared_ptr<const ModelAsset>> removedAssets;
        const std::function<void(flecs::entity)> destroy = [&](flecs::entity current) {
            if (!current.is_valid() || !current.is_alive()) return;
            std::vector<flecs::entity> children;
            current.children([&](flecs::entity child) { children.push_back(child); });
            for (auto child : children) destroy(child);
            if (m_physics_system && m_physics_system->get_system() && current.has<PhysicsBody>()) {
                auto& bodies = m_physics_system->get_body_interface();
                const JPH::BodyID id(current.get<PhysicsBody>().bodyID);
                JPH::BodyLockRead lock(m_physics_system->get_system()->GetBodyLockInterface(), id);
                const bool exists = lock.Succeeded();
                lock.ReleaseLock();
                if (exists) {
                    if (bodies.IsAdded(id)) bodies.RemoveBody(id);
                    bodies.DestroyBody(id);
                }
                current.remove<PhysicsBody>();
            }
            if (current.has<ModelAssetReference>()) {
                const auto& asset = current.get<ModelAssetReference>().asset;
                if (asset) removedAssets.try_emplace(asset.get(), asset);
            }
            current.destruct();
        };
        destroy(entity);
        std::unordered_set<const ModelAsset*> referenced;
        m_world->each([&](const ModelAssetReference& reference) { referenced.insert(reference.asset.get()); });
        for (const auto& [pointer, asset] : removedAssets) if (!referenced.contains(pointer)) {
            for (std::size_t i = 0; i < asset->model.meshes.size(); ++i)
                mModel.meshes.at(asset->firstMesh + i) = EngineMesh{};
        }
        std::unordered_set<std::string> imports;
        m_world->each([&](const ModelPartSource& source) { imports.insert(source.instance); });
        std::erase_if(mModelImports, [&](const auto& source) { return !imports.contains(source.id); });
    }

    flecs::entity SceneManager::load_static_model(const EngineModel& model, uint32_t baseMeshIdx, uint32_t baseMatIdx, RenderLayer layer) {
        std::print("Loading static model with {} instances\n", model.scenes.size());
        cache_model_for_culling(model, baseMeshIdx, baseMatIdx);
        flecs::entity firstEntity = flecs::entity::null();

        for (const auto& instance : model.scenes) {
            std::string name = instance.name.empty() ? "StaticObject" : instance.name;
            name += "_" + std::to_string(mStaticEntityCounter++);
            while (m_world->lookup(name.c_str()).is_alive())
                name = (instance.name.empty() ? "StaticObject" : instance.name) + "_" + std::to_string(mStaticEntityCounter++);

            auto e = m_world->entity(name.c_str())
                .add<StaticObject>()
                .set<EntityStatus>({ true, true })
                .set<LayerComponent>({ layer })
                .set<LocalTransform>({ instance.transform })
                .set<WorldTransform>({ instance.transform })
                .set<MeshComponent>({ instance.meshIndex + baseMeshIdx });

            uint32_t matIdx = model.meshes[instance.meshIndex].materialIndex + baseMatIdx;
            e.set<MaterialComponent>({ matIdx });

            RecordModelPart(e);

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

        for (const auto& instance : model.scenes) {
            std::string name = instance.name.empty() ? "DynamicObject" : instance.name;
            name += "_" + std::to_string(mDynamicEntityCounter++);
            while (m_world->lookup(name.c_str()).is_alive())
                name = (instance.name.empty() ? "DynamicObject" : instance.name) + "_" + std::to_string(mDynamicEntityCounter++);

            auto e = m_world->entity(name.c_str())
                .add<DynamicObject>()
                // 【核心修改 1】：如果 mass 是负数，说明我们不需要物理，关闭物理标记
                .set<EntityStatus>({ true, mass >= 0.0f })
                .set<LayerComponent>({ layer })
                .set<LocalTransform>({ instance.transform })
                .set<WorldTransform>({ instance.transform })
                .set<MeshComponent>({ instance.meshIndex + baseMeshIdx });

            uint32_t matIdx = model.meshes[instance.meshIndex].materialIndex + baseMatIdx;
            e.set<MaterialComponent>({ matIdx });

            RecordModelPart(e);

            // 【核心修改 2】：只有 mass >= 0 时，才生成物理刚体！
            if (m_physics_system && mass >= 0.0f) {
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

        ;
        for (const auto& instance : model.scenes) {
            std::string name = instance.name.empty() ? "CompoundPart" : instance.name;
            name += "_" + std::to_string(mCompoundEntityCounter++);
            while (m_world->lookup(name.c_str()).is_alive())
                name = (instance.name.empty() ? "CompoundPart" : instance.name) + "_" + std::to_string(mCompoundEntityCounter++);

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

            RecordModelPart(e);

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

        for (const auto& instance : model.scenes) {
            std::string name = instance.name.empty() ? "CPart" : instance.name;
            name += "_" + std::to_string(mCustomEntityCounter++);
            while (m_world->lookup(name.c_str()).is_alive())
                name = (instance.name.empty() ? "CPart" : instance.name) + "_" + std::to_string(mCustomEntityCounter++);

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

            RecordModelPart(e);

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

    void SceneManager::load_animated_model(
        const EngineModel& model,
        uint32_t baseMeshIdx, uint32_t baseMatIdx,
        uint32_t baseSkinIdx, uint32_t baseAnimIdx, std::shared_ptr<const ModelAsset> asset)
    {
        std::print("Loading animated model with {} instances, {} skins, {} anims\n",
            model.scenes.size(), model.skins.size(), model.animations.size());
        for (size_t i = 0; i < model.meshes.size(); ++i) {
            std::print("  mesh[{}]: {} verts, {} indices, isSkinned={}\n",
                i, model.meshes[i].positions.size(),
                model.meshes[i].indices.size(),
                model.meshes[i].isSkinned);
        }

        cache_model_for_culling(model, baseMeshIdx, baseMatIdx);

        for (const auto& instance : model.scenes) {
            std::string name = instance.name.empty() ? "AnimatedObject" : instance.name;
            name += "_" + std::to_string(mAnimatedEntityCounter++);

            uint32_t globalMeshIdx = instance.meshIndex + baseMeshIdx;
            auto e = m_world->entity(name.c_str())
                .add<DynamicObject>()
                .set<EntityStatus>({ true, false })  // no individual physics by default
                .set<LocalTransform>({ instance.transform })
                .set<WorldTransform>({ instance.transform })
                .set<MeshComponent>({ globalMeshIdx });

            uint32_t matIdx = model.meshes[instance.meshIndex].materialIndex + baseMatIdx;
            e.set<MaterialComponent>({ matIdx });
            if (asset) e.set<ModelAssetReference>({asset});

            std::print("  entity '{}': globalMesh={}, mat={}, skinIdx={}\n",
                name, globalMeshIdx, matIdx, instance.skinIndex);

            if (instance.skinIndex >= 0) {
                SkinComponent sc{};
                sc.skinIndex = (int)(baseSkinIdx + instance.skinIndex);
                // Pre-size bone matrices (actual count set by AnimationSystem on first update)
                if (instance.skinIndex < (int)model.skins.size())
                    sc.boneMatrices.resize(model.skins[instance.skinIndex].joints.size(), glm::mat4(1.0f));
                std::print("    SkinComponent: skinIdx={}, joints={}\n",
                    sc.skinIndex, sc.boneMatrices.size());
                e.set<SkinComponent>(std::move(sc));

                // Auto-assign first available animation (if any)
                if (!model.animations.empty()) {
                    AnimationComponent ac{};
                    ac.animIndex = (int)baseAnimIdx; // first animation
                    ac.playing = true;
                    ac.looping = true;
                    ac.speed = 1.0f;
                    e.set<AnimationComponent>(std::move(ac));
                }
            }
        }
    }

    std::vector<RenderBatch> SceneManager::get_skinned_batches(
        glm::mat4* boneBuffer, size_t maxBones, size_t& outBoneCount)
    {
        std::vector<RenderBatch> batches;
        size_t boneOffset = 0;

        m_world->query<WorldTransform, MeshComponent, MaterialComponent, SkinComponent, const OpacityComponent*>()
            .each([&](flecs::entity e,
                WorldTransform& wt,
                MeshComponent& mc,
                MaterialComponent& matc,
                SkinComponent& sc,
                const OpacityComponent* op)
                {
                    if (!scene_hierarchy::Visible(e))
                        return;
                    if (sc.boneMatrices.empty())
                        return;

                    size_t boneCount = sc.boneMatrices.size();
                    if (boneBuffer && (boneOffset + boneCount) <= maxBones) {
                        std::memcpy(boneBuffer + boneOffset,
                            sc.boneMatrices.data(),
                            boneCount * sizeof(glm::mat4));
                    }

                    RenderBatch batch{};
                    batch.meshIndex = mc.meshIndex;
                    batch.materialIndex = matc.materialIndex;
                    batch.transform = wt.matrix;
                    batch.ditherFade = op ? op->currentFade : 1.0f;
                    batch.entityId = e.id();
                    batch.isSkinned = true;
                    batch.boneBaseIndex = static_cast<uint32_t>(boneOffset);
                    if (e.has<RiderBinding>()) {
                        batch.attachedEntityId = e.get<RiderBinding>().bikeEntityId;
                    }
                    batches.push_back(batch);

                    boneOffset += boneCount;
                });

        outBoneCount = boneOffset;
        return batches;
    }

    std::vector<::RenderBatch> SceneManager::BuildSkinnedBatches(
        glm::mat4* boneBuffer, std::size_t maxBones, std::size_t& outBoneCount) {
        return get_skinned_batches(boneBuffer, maxBones, outBoneCount);
    }


    // =========================================================================
    // 灯光与渲染数据提取
    // =========================================================================
    flecs::entity SceneManager::create_light_entity(
        const char* name, LightType type, glm::vec3 color, float intensity,
        const glm::mat4& transform, float range, glm::vec3 direction,
        float innerCutOff, float outerCutOff, flecs::entity parent)
    {
        // 【修改】：使用结构体明确赋值，防止成员增加导致初始化错位
        LightComponent lc{};
        lc.color = color;
        lc.intensity = intensity;
        lc.type = type;
        lc.range = range;
        lc.specularMultiplier = 1.0f; // 默认所有灯光都产生正常高光
        lc.direction = direction;
        lc.innerCutOff = innerCutOff;
        lc.outerCutOff = outerCutOff;

        glm::mat4 authoredTransform = transform;
        if (type == LightType::Directional && std::abs(transform[3][3]) < 0.0001f) {
            // Migrate native callers that formerly stored a toward-light vector
            // in the translation column of a non-affine transform.
            lc.direction = -NormalizeLightDirection(glm::vec3(transform[3]));
            authoredTransform[3] = glm::vec4(0, 0, 0, 1);
        }
        lc.direction = NormalizeLightDirection(lc.direction);
        lc.primaryDirectional = type == LightType::Directional && std::string(name) == "MainSun";
        if (type != LightType::Directional) lc.range = std::max(range, 0.001f);

        auto e = m_world->entity(name)
            .set<EntityStatus>({ true, false })
            .set<LocalTransform>({ authoredTransform })
            .set<WorldTransform>({ authoredTransform })
            .set<LightComponent>(lc); // 传入打包好的组件

        if (parent.is_valid()) {
            e.child_of(parent);
        }

        return e;
    }
    void SceneManager::get_light_data(std::vector<GpuLight>& outLights) {
        outLights = GetLightingData().lights;
    }
    SceneLightingData SceneManager::GetLightingData() {
        std::vector<SceneLightCandidate> candidates;
        m_world->query<const WorldTransform, const LightComponent, const EntityStatus>()
            .each([&](flecs::entity entity, const WorldTransform& wt, const LightComponent& light, const EntityStatus& status) {
                candidates.push_back({entity.id(), entity.name().c_str() ? entity.name().c_str() : "", wt.matrix, light, scene_hierarchy::Visible(entity)});
            });
        return BuildSceneLighting(std::move(candidates));
    }

    std::vector<RenderBatch> SceneManager::get_render_batches(const Frustum* frustum, float frustumPadding,
        glm::vec3 cameraPos, bool updateFrustumStats) {
        std::vector<RenderBatch> batches;
        uint32_t candidates = 0;
        uint32_t visible = 0;

        const bool doLOD = mState && mState->render.lodEnabled;

        m_world->query<const WorldTransform, const MeshComponent, const MaterialComponent, const EntityStatus, const OpacityComponent*, const LODComponent*>()
            .each([&](flecs::entity e, const WorldTransform& wt, const MeshComponent& mc, const MaterialComponent& matc, const EntityStatus& status, const OpacityComponent* op, const LODComponent* lod) {

            if (!scene_hierarchy::Visible(e)) return;
            if (e.has<SkinComponent>()) return;
            ++candidates;

            if (frustum && mc.meshIndex < mModel.meshes.size()) {
                const EngineMesh& mesh = mModel.meshes[mc.meshIndex];

                glm::vec3 worldAabbMin(0.0f);
                glm::vec3 worldAabbMax(0.0f);
                TransformAabb(mesh.localAabbMin, mesh.localAabbMax, wt.matrix, worldAabbMin, worldAabbMax);

                if (!IntersectsAabb(*frustum, worldAabbMin, worldAabbMax, frustumPadding)) {
                    return;
                }
            }

            ++visible;

            // --- LOD selection ---
            uint32_t selectedMesh = mc.meshIndex;
            if (doLOD && lod && lod->levelCount > 0) {
                glm::vec3 entityPos = glm::vec3(wt.matrix[3]);
                float dist = mState->editor.lodDebugDistance >= 0.0f
                    ? mState->editor.lodDebugDistance
                    : glm::length(entityPos - cameraPos);
                float distSq = dist * dist;
                float biasSq = lod->lodBias * lod->lodBias;
                for (int i = 0; i < lod->levelCount; ++i) {
                    if (distSq > lod->levels[i].maxDistSq * biasSq)
                        selectedMesh = lod->levels[i].meshIndex;
                }
            }

            float ditherFade = op ? op->currentFade : 1.0f;

            // 检查实体的渲染层决定是否投射阴影
            bool castsShadow = true;
            if (e.has<LayerComponent>()) {
                if (e.get<LayerComponent>().layer == RenderLayer::Emissive) {
                    castsShadow = false;
                }
            }

            RenderBatch batch{};
            batch.meshIndex = selectedMesh;
            batch.materialIndex = matc.materialIndex;
            batch.transform = wt.matrix;
            batch.ditherFade = ditherFade;
            batch.entityId = e.id();
            batch.castShadow = castsShadow;
            if (e.has<CompoundParent>()) {
                batch.compoundBodyID = e.get<CompoundParent>().bodyID;
            }
            else if (e.has<PhysicsBody>()) {
                batch.compoundBodyID = e.get<PhysicsBody>().bodyID;
            }
            batches.push_back(batch);
                });
        if (updateFrustumStats) {
            mLastFrustumCullingCandidates = candidates;
            mLastFrustumCullingVisible = visible;
        }
        return batches;
    }

    std::vector<::RenderBatch> SceneManager::BuildRenderBatches(const SceneRenderRequest& request) {
        return get_render_batches(request.frustum, request.frustumPadding, request.cameraPosition, request.updateFrustumStats);
    }

    void SceneManager::SetupEntityLOD(flecs::entity e,
                                       std::initializer_list<uint32_t> lodMeshIndices,
                                       std::initializer_list<float>    distanceThresholds)
    {
        if (!e.is_valid()) return;

        const size_t count = lodMeshIndices.size();
        if (count == 0 || count != distanceThresholds.size()) return;
        if (count > static_cast<size_t>(LODComponent::kMaxLevels)) return;

        LODComponent lod{};
        lod.levelCount = static_cast<int>(count);

        auto meshIt = lodMeshIndices.begin();
        auto distIt = distanceThresholds.begin();
        for (int i = 0; i < lod.levelCount; ++i, ++meshIt, ++distIt) {
            lod.levels[i].meshIndex = *meshIt;
            lod.levels[i].maxDistSq = (*distIt) * (*distIt);
        }

        e.set<LODComponent>(lod);
    }


    // =========================================================================
    // 每帧更新 (Physics Sync, Animations, etc)
    // =========================================================================

    void SceneManager::Update(float dt) {
        // Keep transform propagation alive for editor changes while simulation is paused.
        if (m_physics_system && (!mState || mState->frame.runSimulationSystems)) {
            JPH::BodyInterface& bodyInterface = m_physics_system->get_body_interface();

            // 1. 同步普通刚体位置
            std::unordered_map<flecs::entity_t, glm::mat4> simulatedWorld;
            m_world->query<LocalTransform, const PhysicsBody, const EntityStatus>()
                .each([&](flecs::entity e, LocalTransform& lt, const PhysicsBody& pb, const EntityStatus& status) {
                if (!status.has_physics) return;
                JPH::BodyID bodyID(pb.bodyID);

                if (bodyInterface.IsActive(bodyID)) {
                    JPH::Vec3 pos = bodyInterface.GetPosition(bodyID);
                    JPH::Quat rot = bodyInterface.GetRotation(bodyID);

                    glm::mat4 translation = glm::translate(glm::mat4(1.0f), toGlm(pos));
                    glm::mat4 rotation = glm::mat4_cast(toGlm(rot));

                    if (const auto pose = DecomposePhysicsTransform(AuthoredWorldTransform(e)))
                        simulatedWorld.emplace(e.id(), translation * rotation * glm::scale(glm::mat4(1.0f), pose->scale));
                }
                    });
            // Jolt supplies world poses. Convert them back to parent-local poses
            // only after all simulated parents have been sampled this frame.
            std::function<glm::mat4(flecs::entity)> resolvedPhysicsWorld = [&](flecs::entity entity) {
                if (!entity.is_valid() || !entity.is_alive() || !entity.has<LocalTransform>()) return glm::mat4(1.0f);
                if (auto found = simulatedWorld.find(entity.id()); found != simulatedWorld.end()) return found->second;
                return resolvedPhysicsWorld(entity.parent()) * entity.get<LocalTransform>().matrix;
            };
            for (const auto& [id, transform] : simulatedWorld) {
                auto entity = m_world->entity(id);
                entity.set<LocalTransform>({glm::inverse(resolvedPhysicsWorld(entity.parent())) * transform});
            }

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
                            float steerAngle = mState ? mState->player.State().bikeSteerAngle : 0.0f;
                            glm::mat4 steerRot = glm::rotate(glm::mat4(1.0f), steerAngle, glm::vec3(0.0f, 1.0f, 0.0f));
                            finalOffset = finalOffset * steerRot;
                        }

                        if (strstr(name, "frontwheel.001_3") || strstr(name, "Rear wheel_1") || strstr(name, "Pedal.002_7") || strstr(name, "WholePedal_6")) {
                            float currentSpeed = mState ? mState->player.State().bikeSpeed : 0.0f;
                            speed += 0.001f * currentSpeed;
                            glm::mat4 selfRot = glm::rotate(glm::mat4(1.0f), speed, glm::vec3(1.0f, 0.0f, 0.0f));
                            finalOffset = finalOffset * selfRot;
                        }
                    }
                    lt.matrix = bodyWorld * finalOffset;
                }
                    });

            // =========================================================
       // Pass 2: child-part inheritance
       // handle_left/right follow steer.001_2; pedal_left/right follow WholePedal_6
       // Formula: childWorld = parentWorld * inv(parentLocalOff) * childLocalOff
       // =========================================================
            {
                auto inherit_from_parent = [&](const char* parentName,
                    std::initializer_list<const char*> childNames)
                    {
                        flecs::entity parentEnt = find_entity(parentName);
                        if (!parentEnt.is_valid() ||
                            !parentEnt.has<LocalTransform>() ||
                            !parentEnt.has<CompoundParent>()) return;

                        const glm::mat4  parentWorld = parentEnt.get<LocalTransform>().matrix;
                        const glm::mat4  parentLocalOff = parentEnt.get<CompoundParent>().localOffset;
                        const glm::mat4  invParentOff = glm::inverse(parentLocalOff);

                        m_world->query<LocalTransform, const CompoundParent>()
                            .each([&](flecs::entity e, LocalTransform& lt, const CompoundParent& cp) {
                            const char* n = e.name();
                            if (!n) return;
                            for (const char* child : childNames) {
                                if (strstr(n, child)) {
                                    lt.matrix = parentWorld * invParentOff * cp.localOffset;
                                    break;
                                }
                            }
                                });
                    };

                inherit_from_parent("steer.001_2", { "handle_left_4", "handle_right_5" });
                inherit_from_parent("WholePedal_6", { "pedal_left_8",  "pedal_right_9" });
            }
            // =========================================================

            // 3. 第三人称相机遮挡透视 (X-Ray) 逻辑
            if (mState && mState->camera.State().thirdPersonMode) {
                const CameraState& cameraState = mState->camera.State();
                glm::vec3 cameraPos = glm::vec3(cameraState.camera2world[3]);

                m_world->defer_begin();
                m_world->query<OpacityComponent>().each([&](flecs::entity e, OpacityComponent& op) {
                    op.currentFade += (op.targetFade - op.currentFade) * cameraState.occlusionFadeSpeed * dt;
                    op.targetFade = 1.0f;
                    if (op.currentFade >= 0.99f && op.targetFade == 1.0f) {
                        e.remove<OpacityComponent>();
                    }
                    });
                m_world->defer_end();

                if (cameraState.occlusionFadeEnabled &&
                    !cameraState.portalCameraActive && !mState->player.State().isExtremeSpeed) {
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
                            bikePos.y += cameraState.occlusionTargetHeight;

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
                                        if (!e.has<OpacityComponent>()) e.set<OpacityComponent>({ 1.0f, cameraState.occlusionFadeCoverage });
                                        else e.get_mut<OpacityComponent>().targetFade = cameraState.occlusionFadeCoverage;
                                    }
                                    });

                                m_world->query<const CompoundParent>().each([&](flecs::entity e, const CompoundParent& cp) {
                                    if (cp.bodyID == hitBodyID) {
                                        if (!e.has<OpacityComponent>()) e.set<OpacityComponent>({ 1.0f, cameraState.occlusionFadeCoverage });
                                        else e.get_mut<OpacityComponent>().targetFade = cameraState.occlusionFadeCoverage;
                                    }
                                    });

                                ignoredIDs.push_back(hitBodyID);
                            }
                            m_world->defer_end();
                        }
                    }
                }
            }
        }
        m_world->query<LocalTransform, const RiderBinding>()
            .each([&](flecs::entity /*e*/, LocalTransform& lt, const RiderBinding& rb) {
            flecs::entity bikeEnt = m_world->entity(rb.bikeEntityId);
            // Use LocalTransform (updated above from physics) so there is no 1-frame lag
            if (bikeEnt.is_valid() && bikeEnt.has<LocalTransform>()) {
                lt.matrix = bikeEnt.get<LocalTransform>().matrix * rb.seatOffset;
            }
                });

        // =========================================================
        // RiderIK: update world-space IK targets from bike/part transforms
        // =========================================================
        m_world->query<RiderIKComponent>()
            .each([&](flecs::entity /*e*/, RiderIKComponent& rik) {
            flecs::entity bikeEnt = m_world->entity(rik.bikeEntityId);
            if (!bikeEnt.is_valid() || !bikeEnt.has<LocalTransform>()) return;
            const glm::mat4& bikeWorld = bikeEnt.get<LocalTransform>().matrix;

            for (auto& chain : rik.chains) {
                // Pole always relative to bike
                chain.worldPole = glm::vec3(bikeWorld * glm::vec4(chain.localBikePole, 1.0f));

                // Target: prefer specific bike-part entity (steer, pedal…)
                if (chain.targetEntityId != 0) {
                    flecs::entity partEnt = m_world->entity(chain.targetEntityId);
                    if (partEnt.is_valid() && partEnt.has<LocalTransform>()) {
                        // Use the part's live LocalTransform (already has steer/crank rotation baked in)
                        const glm::mat4& partWorld = partEnt.get<LocalTransform>().matrix;
                        chain.worldTarget = glm::vec3(partWorld * glm::vec4(chain.localTargetOffset, 1.0f));
                        continue;
                    }
                }
                // Fallback: use bike-local offset
                chain.worldTarget = glm::vec3(bikeWorld * glm::vec4(chain.localBikeTarget, 1.0f));
            }
                });
        if (mState) UpdateSceneWorld(*m_world, mTransformSystem, mState->frame, dt);
        else m_world->progress(dt);
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
