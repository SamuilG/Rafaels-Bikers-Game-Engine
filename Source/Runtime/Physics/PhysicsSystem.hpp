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
#include <Jolt/Physics/Body/BodyLock.h>


#include <memory>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <vector> 
#include "../Core/System.h"
#include "../Scene/model_loader/engine_model.hpp"
#include "../Renderer/RenderUtilities/camera.hpp"
#include "../Input/InputSystem.hpp"
#include "../UserState/UserState.hpp"

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

struct BicycleState {
    JPH::BodyID chassisID = JPH::BodyID(8388679);
    float steerAngle = 0.0f;   // 当前转向角 (rad)
    float currentSpeed = 0.0f;
    float leanAngle = 0.0f;   // 当前倾斜角 (rad)
    float wheelAngle = 0.0f;
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

    void AddForceDirection(const JPH::BodyID& bodyID, float force = 2000.0f);


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

    JPH::BodyID create_dynamic_compound_body(
        const std::vector<EngineMesh>& meshes,
        const std::vector<glm::mat4>& meshTransforms,
        const glm::mat4& transform,
        float mass);
    //=============================UI System Interactions=============================
    // cast_ray
    // Raycast from origin in direction, return first hit BodyID射线检测，返回第一个被击中的物理 BodyID
    uint32_t cast_ray(const glm::vec3& origin, const glm::vec3& direction, float max_distance = 1000.0f);

    uint32_t cast_ray_ignore(const glm::vec3& start, const glm::vec3& end, uint32_t ignoreBodyIDRaw);

    // ...
    uint32_t cast_ray_ignore_multiple(const glm::vec3& start, const glm::vec3& end, const std::vector<uint32_t>& ignoreIDs);

    // 根据 BodyID 同步最新的变换矩阵
    //latest transform
    void set_body_transform(uint32_t bodyID, const glm::mat4& transform);
    //更新缩放
    //update the scale
    void set_body_scale(uint32_t bodyID, const glm::vec3& newScale, const glm::vec3& currentWorldPos, const glm::quat& currentWorldRot);
    //=============================UI System Interactions=============================

    void SetInputSystem(engine::InputSystem* sys) { mInputSystem = sys; }
    void SetUserState(UserState* state) { this->mState = state; }

    void create_bicycle(uint32_t chassisBodyID);
    void update_bicycle(float dt);

    float get_steer_angle() const {
        return m_bicycle ? m_bicycle->steerAngle : 0.0f;
    }

    float get_speed() const {
        return m_bicycle ? m_bicycle->currentSpeed : 0.0f;
    }

    // 【关键修复】：暴露底层的 Jolt PhysicsSystem 给控制器使用！
    JPH::PhysicsSystem* GetJoltSystem() const {
        return m_physicsSystem.get();
    }
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

    engine::InputSystem* mInputSystem = nullptr;
    UserState* mState = nullptr;

    // Optional Event System Link
    EventSystem* m_eventSystem = nullptr;

    class ContactListenerImpl;
    std::unique_ptr<ContactListenerImpl> m_contactListener;


    std::unique_ptr<BicycleState> m_bicycle;



};

} // namespace engine
