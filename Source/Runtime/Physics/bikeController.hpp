#pragma once

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/BodyID.h>
#include <memory>
#include <cstdint>
#include <glm/glm.hpp>

namespace engine { class AudioSystem; 
class PhysicsSystem;
}

// ǰ������ Jolt ����ϵͳ
namespace JPH { class PhysicsSystem; }

namespace engine {
    class PlayerController;
    // InputSystem Ӧ�û����� engine �����
    class InputSystem;

    // ������״̬�ṹ���Ƶ�����
    struct BicycleState {
        JPH::BodyID chassisID;
        float steerAngle = 0.0f;
        float currentSpeed = 0.0f;
        float leanAngle = 0.0f;
    };

    class BikeController {
    public:
        // ���캯����ע������ʹ�õ���ȫ�ֵ� UserState
        BikeController(JPH::PhysicsSystem* joltPhysics, InputSystem* input, PlayerController* player);
        ~BikeController() = default;

        void Init(uint32_t chassisBodyID);
        void Update(float dt);
        void SampleMotion();
        void SetAudioSystem(AudioSystem* audio) { m_audio = audio; }

        float get_steer_angle() const { return m_bicycle ? m_bicycle->steerAngle : 0.0f; }
        float get_speed() const { return m_bicycle ? m_bicycle->currentSpeed : 0.0f; }
        JPH::PhysicsSystem* m_joltPhysics = nullptr;

    private:
        InputSystem* m_inputSystem = nullptr;
        PlayerController* m_player = nullptr;
        std::uint64_t m_motionResetRevision = 0;
        float m_engineForce = 0.0f;
        int m_lastPedal = -1;
        AudioSystem* m_audio = nullptr;


        std::unique_ptr<BicycleState> m_bicycle;
    };

} // namespace engine