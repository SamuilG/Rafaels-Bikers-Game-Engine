#pragma once

#if defined(__linux__) || defined(__linux)
#undef Convex
#undef None
#undef Success
#undef Always
#undef True
#undef False
#undef Status
#endif

#include <Jolt/Jolt.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Character/CharacterVirtual.h>


#include <memory>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "../Core/System.h"
#include "../Scene/model_loader/engine_model.hpp"

// helper to convert GLM to Jolt
inline JPH::Vec3 toJolt(const glm::vec3& v) { return JPH::Vec3(v.x, v.y, v.z); }
// helper to convert Jolt to GLM
inline glm::vec3 toGlm(const JPH::Vec3& v) { return glm::vec3(v.GetX(), v.GetY(), v.GetZ()); }
// helper to convert GLM quat to Jolt quat
inline JPH::Quat toJolt(const glm::quat& q) { return JPH::Quat(q.x, q.y, q.z, q.w); }
// helper to convert Jolt quat to GLM quat
inline glm::quat toGlm(const JPH::Quat& q) { return glm::quat(q.GetW(), q.GetX(), q.GetY(), q.GetZ()); }

namespace Layers
{
	static constexpr JPH::ObjectLayer NON_MOVING = 0;
	static constexpr JPH::ObjectLayer MOVING = 1;
	static constexpr JPH::ObjectLayer NUM_LAYERS = 2;
};

namespace engine {
class EventSystem;

class PhysicsSystem final : public System {
public:
    PhysicsSystem();
    ~PhysicsSystem() override;

    void Init() override;
    void optimize_broad_phase();
    void Update(float dt) override;
    void Shutdown() override;

    void SetEventSystem(EventSystem* sys) { m_eventSystem = sys; }

    JPH::PhysicsSystem* get_system() { return m_physicsSystem.get(); }
    JPH::BodyInterface& get_body_interface() { return m_physicsSystem->GetBodyInterface(); }

    // dynamic sphere body at 'position' with given 'radius'.
    // returns the BodyID (use .GetIndexAndSequenceNumber() to pass to ECS).
    JPH::BodyID create_sphere_body(const glm::vec3& position, float radius = 0.5f);

    // Create a large static ground plane body at the given y elevation.
    void create_ground_plane(float y = -0.5f);

    // Create a static triangle mesh body from EngineMesh vertices and indices
    JPH::BodyID create_static_mesh_body(const EngineMesh& mesh, const glm::mat4& transform);

    // Create a dynamic convex hull body from EngineMesh vertices
    JPH::BodyID create_dynamic_convex_body(const EngineMesh& mesh, const glm::mat4& transform, float mass = 1.0f);



private:
    std::unique_ptr<JPH::TempAllocatorImpl> m_tempAllocator;
    std::unique_ptr<JPH::JobSystemThreadPool> m_jobSystem;
    std::unique_ptr<JPH::PhysicsSystem> m_physicsSystem;

    // filters and layers
    class BPLayerInterfaceImpl;
    class ObjectVsBroadPhaseLayerFilterImpl;
    class ObjectLayerPairFilterImpl;

    std::unique_ptr<BPLayerInterfaceImpl> m_bpLayerInterface;
    std::unique_ptr<ObjectVsBroadPhaseLayerFilterImpl> m_objectVsBroadphaseFilter;
    std::unique_ptr<ObjectLayerPairFilterImpl> m_objectVsObjectFilter;

    // Optional Event System Link
    EventSystem* m_eventSystem = nullptr;

    class ContactListenerImpl;
    std::unique_ptr<ContactListenerImpl> m_contactListener;




};

} // namespace engine
