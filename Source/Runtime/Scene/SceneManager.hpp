#pragma once
#include <vector>
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <numbers>

// X11 defines macros that conflict with flecs
#ifdef Bool
#undef Bool
#endif
#ifdef None
#undef None
#endif

#include <flecs.h>
#include "../Core/System.h"
#include "model_loader/engine_model.hpp"
#include "../Renderer/RenderUtilities/light.hpp"
#include "../UserState/UserState.hpp"
#include "../Renderer/RenderUtilities/frustum.hpp"

// Forward declare EngineModel to avoid including engine_model.hpp here
// 1. Avoid circular dependency
// 2. Speed up compilation
// 3. Hide implementation details
//struct EngineModel;

namespace cfg
{
    constexpr char const* ModelPath = "Assets/Models/TScene.glb";
}

namespace engine {
    class PhysicsSystem; // forward declare PhysicsSystem
    class RenderSystem;

    enum class ModelPhysicsType {
        Static,   
        Dynamic,  
        Compound, 
        CustomC   
    };

    enum class RenderLayer {
        Default = 0,
        Emissive = 1,  
        Transparent = 2
    };

    // ���㼶��װΪ Flecs ���
    struct LayerComponent {
        RenderLayer layer = RenderLayer::Default;
    };
}

// Basic components definition (kept in header as they might be queried by other systems)
struct LocalTransform { glm::mat4 matrix; };
struct WorldTransform { glm::mat4 matrix; };
struct MeshComponent { uint32_t meshIndex; };
struct MaterialComponent { uint32_t materialIndex; };

struct PhysicsBody {
    uint32_t bodyID;
};

struct CompoundParent {
    uint32_t bodyID;
    glm::mat4 localOffset;
};

// Tag components
struct StaticObject {};
struct DynamicObject {};

// Entity status component to control rendering and physics behavior
struct EntityStatus {
    bool should_render = true;
    bool has_physics = true;
};

// Render batch data
struct RenderBatch {
    uint32_t  meshIndex;
    uint32_t  materialIndex;
    glm::mat4 transform;
    float     alphaMultiplier = 1.0f;
    bool      castShadow = true; // ������Ⱦ���������Ҫ��Ҫ����Ӱ

    // Skinning (only valid when isSkinned == true)
    bool      isSkinned = false;
    uint32_t  boneBaseIndex = 0;  // offset into the bone SSBO
};

// forward declare EngineModel to avoid including engine_model.hpp here
struct EngineMesh;

// generate a UV sphere mesh
struct EngineMesh generate_uv_sphere(float radius, uint32_t rings, uint32_t sectors);

// Forward declare flecs::world so it can be used as a pointer
namespace flecs { class world; }

struct OpacityComponent {
    float currentAlpha = 1.0f; // ��ǰ͸����
    float targetAlpha = 1.0f; // Ŀ��͸���� (����ƽ������)
};

// Distance-based LOD component.
// LOD0 is implicit: it is always MeshComponent::meshIndex (highest detail, close range).
// Each Level stores a lower-detail mesh index and the squared distance at which it activates.
// Levels must be in ascending distance order: levels[0] is LOD1, levels[1] is LOD2, etc.
struct LODComponent {
    static constexpr int kMaxLevels = 3;
    struct Level {
        uint32_t meshIndex = 0;
        float    maxDistSq = 0.0f; // switch to this level when distSq exceeds previous threshold
    };
    int   levelCount          = 0;
    Level levels[kMaxLevels]{};
    float lodBias             = 1.0f; // per-entity scale multiplier on all distance thresholds
};

// Binds a character entity to follow a bike entity (seat positioning + orientation)
struct RiderBinding {
    uint64_t  bikeEntityId = 0;
    glm::mat4 seatOffset = glm::mat4(1.0f); // character root in bike-local space
};

// Controls which animation clip plays and at what time
struct AnimationComponent {
    int   animIndex = -1;   // index into AnimationSystem's animation list
    float currentTime = 0.0f;
    bool  looping = true;
    float speed = 1.0f;
    bool  playing = true;
};

// Skinning component: bone matrices updated each frame by AnimationSystem
struct SkinComponent {
    int skinIndex = -1;                     // index into AnimationSystem's skin list
    std::vector<glm::mat4> boneMatrices;    // one per joint, computed each frame
};


namespace engine {

    class SceneManager final : public System {

    private:
        struct UserState* mState = nullptr; // �������ָ��

    public:
        SceneManager(PhysicsSystem* physics_system = nullptr);
        ~SceneManager() override;

        void Init() override;
        void Update(float dt) override;
        void Shutdown() override;

    public:
        // Core Model Loading API (returning flecs::entity handle and supporting RenderLayer)
        flecs::entity LoadModel(engine::RenderSystem* renderSystem, const char* path, ModelPhysicsType physicsType, float mass = 1.0f, const glm::mat4& initialTransform = glm::mat4(1.0f), RenderLayer layer = RenderLayer::Default);

        flecs::entity load_static_model(const EngineModel& model, uint32_t baseMeshIdx, uint32_t baseMatIdx, RenderLayer layer);
        flecs::entity load_dynamic_model(const EngineModel& model, float mass, uint32_t baseMeshIdx, uint32_t baseMatIdx, RenderLayer layer);
        flecs::entity load_compound_model(const EngineModel& model, float mass, uint32_t baseMeshIdx, uint32_t baseMatIdx, RenderLayer layer);
        flecs::entity load_C_model(const EngineModel& model, float mass, uint32_t baseMeshIdx, uint32_t baseMatIdx, RenderLayer layer);

        // Load a skinned/animated model: creates entities with AnimationComponent + SkinComponent.
        void load_animated_model(const EngineModel& model,
            uint32_t baseMeshIdx, uint32_t baseMatIdx,
            uint32_t baseSkinIdx, uint32_t baseAnimIdx);

        // Build render batches for skinned entities.
        std::vector<RenderBatch> get_skinned_batches(glm::mat4* boneBuffer, size_t maxBones, size_t& outBoneCount);

        const EngineModel& get_model() const { return mModel; } // expose the cpu model to other systems (like Vulkan) to upload data to gpu

        // Get render batches for the current frame with Frustum Culling and LOD selection.
        // cameraPos: world-space camera position used for distance-based LOD (ignored when lodEnabled is false).
        std::vector<RenderBatch> get_render_batches(const Frustum* frustum = nullptr, float frustumPadding = 0.0f, glm::vec3 cameraPos = glm::vec3(0.0f));

        // Attach LOD levels to an existing entity.
        // lodMeshIndices: globally-registered mesh indices for LOD1, LOD2, ... (LOD0 = MeshComponent::meshIndex).
        // distanceThresholds: world-space distances at which each level activates (must match lodMeshIndices size).
        void SetupEntityLOD(flecs::entity e,
                            std::initializer_list<uint32_t> lodMeshIndices,
                            std::initializer_list<float>    distanceThresholds);

        uint32_t get_last_frustum_culling_candidates() const { return mLastFrustumCullingCandidates; }
        uint32_t get_last_frustum_culling_visible() const { return mLastFrustumCullingVisible; }

        // dynamic entity backed by a runtime mesh + optional physics body
        flecs::entity create_dynamic_entity(const char* name, uint32_t meshIndex, uint32_t matIndex,
            const glm::mat4& transform, uint32_t physicsBodyID = ~0u);

        // 1. Find entity by name (Flecs supports entity naming)
        flecs::entity find_entity(const char* name);

        // 2. Update entity's local transform (position, rotation, scale)
        void set_local_transform(flecs::entity e, const glm::mat4& transform);

        // 3. Utility: Get Flecs world handle for low-level operations
        flecs::world& get_world() { return *m_world; }

        // 4. Utility: Get entity name from physics body ID
        std::string get_entity_name_from_body_id(uint32_t bodyID);

        //==========UI System======================
        // ��ȡ��ǰ������ʵ������������ڵ��Ժ�UI��ʾ��
        int get_entity_count() {
            if (!m_world) return 0;
            return m_world->count<MeshComponent>();
        }

        // Raycast from origin in direction, return first hit entity ���߼�⣬���ص�һ�������е�ʵ��
        flecs::entity raycast_entity(const glm::vec3& origin, const glm::vec3& direction, float max_distance = 1000.0f);
        PhysicsSystem* get_physics_system() const { return m_physics_system; }

        void SetUserState(struct UserState* state) { mState = state; }
        //==========UI System======================

        float speed = 0.0f;
        void print_all_entities();

        // �����ƹ�ϵͳʵ�� (���� parent ����)
        flecs::entity create_light_entity(
            const char* name,
            LightType type,
            glm::vec3 color,
            float intensity,
            const glm::mat4& transform,
            float range = 10.0f,
            glm::vec3 direction = glm::vec3(0.0f, 0.0f, -1.0f),
            float innerCutOff = 12.5f,
            float outerCutOff = 17.5f,
            flecs::entity parent = flecs::entity::null()
        );

        void get_light_data(std::vector<GpuLight>& outLights);

    private:
        flecs::world* m_world;
        EngineModel mModel;
        PhysicsSystem* m_physics_system; // Optional dependency

        void cache_model_for_culling(const EngineModel& model, uint32_t baseMeshIdx, uint32_t baseMatIdx);

        uint32_t mLastFrustumCullingCandidates = 0;
        uint32_t mLastFrustumCullingVisible = 0;
    };

} // namespace engine