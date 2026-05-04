#pragma once
#include <vector>
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <numbers>
#include <flecs.h>
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
#include"../Renderer/RenderUtilities/light.hpp"
#include "../UserState/UserState.hpp"
#include "../Renderer/RenderUtilities/frustum.hpp"

// Forward declare EngineModel to avoid including engine_model.hpp here

// 1. Avoid circular dependency: if engine_model.hpp also included scene_manager.hpp, it would cause compilation errors
// 2. Speed up compilation: forward declaration is more lightweight than including the full header, reducing compile time
// 3. Hide implementation details: SceneManager only needs to know EngineModel exists, without knowing its internals

//struct EngineModel;

namespace cfg
{
    constexpr char const* ModelPath = "Assets/Models/TScene.glb";


}


namespace engine {
    class PhysicsSystem; // forward declare PhysicsSystem
}
namespace engine {
    class RenderSystem;

    // 在 SceneManager 的头文件中定义清晰的物理类型枚举
    enum class ModelPhysicsType {
        Static,   // 静态场景（如桥梁、地面）
        Dynamic,  // 普通动态刚体
        Compound, // 复合碰撞体
        CustomC   // 特殊定制类型（如单车）
    };

    // 1. 定义实体所处的渲染层
    enum class RenderLayer {
        Default = 0,
        Emissive = 1,  // 自发光层（用于跳过阴影计算）
        Transparent = 2
    };
    // 2. 将层级包装为 Flecs 组件
    struct LayerComponent {
        RenderLayer layer = RenderLayer::Default;
    };


    // 在 SceneManager 类内部新增加载函数
    // 此时它需要传入一个 RenderSystem 指针来呼叫 GPU 服务
    // 在 SceneManager.hpp 内部
}
// Basic components definition (kept in header as they might be queried by other systems)
struct LocalTransform { glm::mat4 matrix; };
struct WorldTransform { glm::mat4 matrix; };
struct MeshComponent { uint32_t meshIndex; };
struct MaterialComponent { uint32_t materialIndex; };
struct SkeletonComponent { uint32_t skeletonIndex; };

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
    uint32_t meshIndex = 0;
    uint32_t materialIndex = 0;
    glm::mat4 transform = glm::mat4(1.0f);
    float alphaMultiplier = 1.0f;
    bool castShadow = true;
    int skeletonIndex = -1;
};

// forward declare EngineModel to avoid including engine_model.hpp here
struct EngineMesh;

// generate a UV sphere mesh
// returned mesh has positions, normals, texcoords, indices
struct EngineMesh generate_uv_sphere(float radius, uint32_t rings, uint32_t sectors);

// Forward declare flecs::world so it can be used as a pointer
namespace flecs { class world; }


struct OpacityComponent {
    float currentAlpha = 1.0f; // 当前透明度
    float targetAlpha = 1.0f;  // 目标透明度 (用于平滑过渡)
};


namespace engine {

class SceneManager final : public System {


private:
    struct UserState* mState = nullptr; // 加上这个指针 (如果不识别 UserState，请 include 对应的头文件)


public:
    SceneManager(PhysicsSystem* physics_system = nullptr);
    ~SceneManager() override;

    void Init() override;
    void Update(float dt) override;
    void Shutdown() override;




public:
    //
    flecs::entity LoadModel(engine::RenderSystem* renderSystem, const char* path, ModelPhysicsType physicsType, float mass = 1.0f, const glm::mat4& initialTransform = glm::mat4(1.0f), RenderLayer layer = RenderLayer::Default);
    flecs::entity load_static_model(const EngineModel& model, uint32_t baseMeshIdx, uint32_t baseMatIdx, RenderLayer layer, uint32_t baseSkeletonIdx = 0);
    flecs::entity load_dynamic_model(const EngineModel& model, float mass, uint32_t baseMeshIdx, uint32_t baseMatIdx, RenderLayer layer, uint32_t baseSkeletonIdx = 0);
    flecs::entity load_compound_model(const EngineModel& model, float mass, uint32_t baseMeshIdx, uint32_t baseMatIdx, RenderLayer layer, uint32_t baseSkeletonIdx = 0);
    flecs::entity load_C_model(const EngineModel& model, float mass, uint32_t baseMeshIdx, uint32_t baseMatIdx, RenderLayer layer, uint32_t baseSkeletonIdx = 0);
	const EngineModel& get_model() const { return mModel; }//expose the cpu model to other systems (like Vulkan) to upload data to gpu








    // Get render batches for the current frame
    //frustum culling
   // std::vector<RenderBatch> get_render_batches(const Frustum* frustum = nullptr);
    std::vector<RenderBatch> get_render_batches(const Frustum* frustum = nullptr, float frustumPadding = 0.0f); // new frustum culling
    uint32_t get_last_frustum_culling_candidates() const { return mLastFrustumCullingCandidates; }
    uint32_t get_last_frustum_culling_visible() const { return mLastFrustumCullingVisible; }

    // dynamic entity backed by a runtime mesh + optional physics body
    // meshIndex: index returned by RenderSystem::add_runtime_mesh()
    // matIndex:  material index (use 0 or default gray)
    // transform: initial world transform
    // physicsBodyID: optional Jolt BodyID (UINT32_MAX = none)
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
    //获取当前场景中实体的数量（用于调试和UI显示）
    // Get the count of entities with MeshComponent, which indicates how many renderable entities are in the scene.
    int get_entity_count() {
        if (!m_world) return 0;
        return m_world->count<MeshComponent>();
    }
    // Raycast from origin in direction, return first hit entity 射线检测，返回第一个被击中的实体
    flecs::entity raycast_entity(const glm::vec3& origin, const glm::vec3& direction, float max_distance = 1000.0f);
    PhysicsSystem* get_physics_system() const { return m_physics_system; }


    void SetUserState(struct UserState* state) { mState = state; }

    //==========UI System======================
    float speed = 0.0f;
    void print_all_entities();
    // 在 SceneManager.hpp 中：
   // SceneManager.hpp

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
    EngineModel mModel;//private member
    PhysicsSystem* m_physics_system; // Optional dependency
    void cache_model_for_culling(const EngineModel& model, uint32_t baseMeshIdx, uint32_t baseMatIdx); //frustum culling
    uint32_t mLastFrustumCullingCandidates = 0; //frustum culling
    uint32_t mLastFrustumCullingVisible = 0; // frustum culling
};

} // namespace engine