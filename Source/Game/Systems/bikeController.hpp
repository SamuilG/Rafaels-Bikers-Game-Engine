#pragma once

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/BodyID.h>
#include <memory>
#include <glm/glm.hpp>

namespace engine { class AudioSystem; 
class PhysicsSystem;
}

// 前锟斤拷锟斤拷锟斤拷 Jolt 锟斤拷锟斤拷系统
namespace JPH { class PhysicsSystem; }

namespace engine {
    struct UserState; // 锟斤拷锟斤拷锟斤拷锟角帮拷锟斤拷锟斤拷锟斤拷锟?
    // InputSystem 应锟矫伙拷锟斤拷锟斤拷 engine 锟斤拷锟斤拷锟?
    class InputSystem;

    // 锟斤拷锟斤拷锟斤拷状态锟结构锟斤拷锟狡碉拷锟斤拷锟斤拷
    struct BicycleState {
        JPH::BodyID chassisID;
        float steerAngle = 0.0f;
        float currentSpeed = 0.0f;
        float leanAngle = 0.0f;
    };

    class BikeController {
    public:
        // 锟斤拷锟届函锟斤拷锟斤拷注锟斤拷锟斤拷锟斤拷使锟矫碉拷锟斤拷全锟街碉拷 UserState
        BikeController(JPH::PhysicsSystem* joltPhysics, InputSystem* input, UserState* state);
        ~BikeController() = default;

        void Init(uint32_t chassisBodyID);
        void Update(float dt);
        void SetAudioSystem(AudioSystem* audio) { m_audio = audio; }

        float get_steer_angle() const { return m_bicycle ? m_bicycle->steerAngle : 0.0f; }
        float get_speed() const { return m_bicycle ? m_bicycle->currentSpeed : 0.0f; }
        JPH::PhysicsSystem* m_joltPhysics = nullptr;

    private:
        PhysicsSystem* m_physicsSystem; // Store your engine's wrapper
        InputSystem* m_inputSystem = nullptr;
        UserState* m_state = nullptr;
        AudioSystem* m_audio = nullptr;


        std::unique_ptr<BicycleState> m_bicycle;
    };

} // namespace engine