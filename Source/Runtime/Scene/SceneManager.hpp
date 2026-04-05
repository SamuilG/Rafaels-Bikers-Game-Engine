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
#include"../Renderer/RenderUtilities/light.hpp"
#include "../UserState/UserState.hpp"

// Forward declare EngineModel to avoid including engine_model.hpp here

// 1. Avoid circular dependency: if engine_model.hpp also included scene_manager.hpp, it would cause compilation errors
// 2. Speed up compilation: forward declaration is more lightweight than including the full header, reducing compile time
// 3. Hide implementation details: SceneManager only needs to know EngineModel exists, without knowing its internals

//struct EngineModel;

namespace cfg
{
    // constexpr char const* ModelPath = "Assets/Models/TScene.glb";
    constexpr char const* ModelPath = "Assets/Models/warehouseSceneWithShelf_opt.glb";



}


namespace engine {
    class PhysicsSystem; // forward declare PhysicsSystem
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
    uint32_t meshIndex;
    uint32_t materialIndex;
    glm::mat4 transform;
    float alphaMultiplier; 
};

// forward declare EngineModel to avoid including engine_model.hpp here
struct EngineMesh;

// generate a UV sphere mesh
// returned mesh has positions, normals, texcoords, indices
struct EngineMesh generate_uv_sphere(float radius, uint32_t rings, uint32_t sectors);

// Forward declare flecs::world so it can be used as a pointer
namespace flecs { class world; }


struct OpacityComponent {
    float currentAlpha = 1.0f; // ��ǰ͸����
    float targetAlpha = 1.0f;  // Ŀ��͸���� (����ƽ������)
};


namespace engine {

class SceneManager final : public System {


private:
    struct UserState* mState = nullptr; // �������ָ�� (�����ʶ�� UserState���� include ��Ӧ��ͷ�ļ�)


public:
    SceneManager(PhysicsSystem* physics_system = nullptr);
    ~SceneManager() override;

    void Init() override;
    void Update(float dt) override;
    void Shutdown() override;

    // Load and instantiate all entities from EngineModel as static
    void load_static_model(const EngineModel& model, uint32_t baseMeshIdx = 0, uint32_t baseMatIdx = 0);

    // Load and instantiate all entities from EngineModel as dynamic
    void load_dynamic_model(const EngineModel& model, float mass = 1.0f, uint32_t baseMeshIdx = 0, uint32_t baseMatIdx = 0);

    void load_compound_model(const EngineModel& model, float mass, uint32_t baseMeshIdx, uint32_t baseMatIdx);

    void load_C_model(const EngineModel& model, float mass, uint32_t baseMeshIdx, uint32_t baseMatIdx);

	const EngineModel& get_model() const { return mModel; }//expose the cpu model to other systems (like Vulkan) to upload data to gpu

    // Get render batches for the current frame
    std::vector<RenderBatch> get_render_batches();

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
    //��ȡ��ǰ������ʵ������������ڵ��Ժ�UI��ʾ��
    // Get the count of entities with MeshComponent, which indicates how many renderable entities are in the scene.
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
    // �� SceneManager.hpp �У�
    flecs::entity create_light_entity(
        const char* name,
        LightType type,
        glm::vec3 color,
        float intensity,
        const glm::mat4& transform,
        float range = 10.0f,
        // --- ���������۹��ר����������Ĭ��ֵ����Ӱ����ǰ�Ĵ��룩 ---
        glm::vec3 direction = glm::vec3(0.0f, 0.0f, -1.0f),
        float innerCutOff = 12.5f,
        float outerCutOff = 17.5f
    );

    void get_light_data(std::vector<GpuLight>& outLights);
private:
    flecs::world* m_world; 
    EngineModel mModel;//private member
    PhysicsSystem* m_physics_system; // Optional dependency
};

} // namespace engine