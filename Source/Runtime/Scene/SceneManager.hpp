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

// Forward declare EngineModel to avoid including engine_model.hpp here

// 1. Avoid circular dependency: if engine_model.hpp also included scene_manager.hpp, it would cause compilation errors
// 2. Speed up compilation: forward declaration is more lightweight than including the full header, reducing compile time
// 3. Hide implementation details: SceneManager only needs to know EngineModel exists, without knowing its internals

struct EngineModel;

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

// Tag components

struct StaticObject {};
struct DynamicObject {};

// Render batch data
struct RenderBatch {
    uint32_t meshIndex;
    uint32_t materialIndex;
    glm::mat4 transform;
};

// forward declare EngineModel to avoid including engine_model.hpp here
struct EngineMesh;

// generate a UV sphere mesh
// returned mesh has positions, normals, texcoords, indices
struct EngineMesh generate_uv_sphere(float radius, uint32_t rings, uint32_t sectors);

// Forward declare flecs::world so it can be used as a pointer
namespace flecs { class world; }

namespace engine {

class SceneManager final : public System {
public:
    SceneManager(PhysicsSystem* physics_system = nullptr);
    ~SceneManager() override;

    void Init() override;
    void Update(float dt) override;
    void Shutdown() override;

    // Load and instantiate all entities from EngineModel
    void load_model(const EngineModel& model);

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

    void print_all_entities();

private:
    flecs::world* m_world; 
    PhysicsSystem* m_physics_system; // Optional dependency
};

} // namespace engine