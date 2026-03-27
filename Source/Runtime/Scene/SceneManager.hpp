#pragma once
#include <vector>
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <numbers>

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

namespace cfg
{
    constexpr char const* ModelPath = "Assets/Models/TScene.glb";
}

namespace engine {
    class PhysicsSystem;
    class InputSystem;
}

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

struct WheelSpin {
    glm::mat4 baseLocalOffset;
    glm::vec3 localAxis{ 1.0f, 0.0f, 0.0f };
    float radius = 1.0f;
    float angle = 0.0f;
    float visualScale = 1.0f;
};

struct SteeringVisual {
    glm::mat4 baseLocalOffset;
    glm::vec3 localAxis{ 0.0f, 1.0f, 0.0f };
    glm::vec3 pivotLocalPosition{ 0.0f };
    float angle = 0.0f;
    float maxAngleRadians = 0.55f;
    float response = 8.0f;
};

struct AttachedToCompoundBody {
    uint32_t bodyID;
    glm::mat4 localOffset;
};

struct StaticObject {};
struct DynamicObject {};

struct EntityStatus {
    bool should_render = true;
    bool has_physics = true;
};

struct RenderBatch {
    uint32_t meshIndex;
    uint32_t materialIndex;
    glm::mat4 transform;
};

struct EngineMesh;
struct EngineMesh generate_uv_sphere(float radius, uint32_t rings, uint32_t sectors);

namespace flecs { class world; }

namespace engine {

class SceneManager final : public System {
public:
    SceneManager(PhysicsSystem* physics_system = nullptr);
    ~SceneManager() override;

    void Init() override;
    void Update(float dt) override;
    void Shutdown() override;

    void load_static_model(const EngineModel& model, uint32_t baseMeshIdx = 0, uint32_t baseMatIdx = 0);
    void load_dynamic_model(const EngineModel& model, float mass = 1.0f, uint32_t baseMeshIdx = 0, uint32_t baseMatIdx = 0);
    void load_compound_model(const EngineModel& model, float mass, uint32_t baseMeshIdx, uint32_t baseMatIdx);
    void load_C_model(const EngineModel& model, float mass, uint32_t baseMeshIdx, uint32_t baseMatIdx);

    const EngineModel& get_model() const { return mModel; }
    std::vector<RenderBatch> get_render_batches();

    flecs::entity create_dynamic_entity(const char* name, uint32_t meshIndex, uint32_t matIndex,
        const glm::mat4& transform, uint32_t physicsBodyID = ~0u);

    flecs::entity find_entity(const char* name);
    void set_local_transform(flecs::entity e, const glm::mat4& transform);
    flecs::world& get_world() { return *m_world; }

    std::string get_entity_name_from_body_id(uint32_t bodyID);
    uint32_t find_compound_body_near(const glm::vec3& worldPos, float maxDistance = 20.0f) const;
    glm::vec3 get_compound_body_world_position(uint32_t bodyID) const;

    int get_entity_count() {
        if (!m_world) return 0;
        return m_world->count<MeshComponent>();
    }

    flecs::entity raycast_entity(const glm::vec3& origin, const glm::vec3& direction, float max_distance = 1000.0f);
    PhysicsSystem* get_physics_system() const { return m_physics_system; }

    void print_all_entities();
    void SetInputSystem(InputSystem* sys) { m_input_system = sys; }

    flecs::entity create_light_entity(
        const char* name,
        LightType type,
        glm::vec3 color,
        float intensity,
        const glm::mat4& transform,
        float range = 10.0f,
        glm::vec3 direction = glm::vec3(0.0f, 0.0f, -1.0f),
        float innerCutOff = 12.5f,
        float outerCutOff = 17.5f
    );

    void get_light_data(std::vector<GpuLight>& outLights);

private:
    flecs::world* m_world = nullptr;
    EngineModel mModel;
    PhysicsSystem* m_physics_system = nullptr;
    InputSystem* m_input_system = nullptr;
};

} // namespace engine
