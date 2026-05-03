#pragma once

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/BodyID.h>
#include <memory>
#include <glm/glm.hpp>
#include "tracy/Tracy.hpp"

// 前向声明 Jolt 物理系统
namespace JPH { class PhysicsSystem; }

namespace engine {
    struct UserState; // 加上这个前向声明！
    // InputSystem 应该还是在 engine 里面的
    class InputSystem;

    // 将单车状态结构体移到这里
    struct BicycleState {
        JPH::BodyID chassisID;
        float steerAngle = 0.0f;
        float currentSpeed = 0.0f;
        float leanAngle = 0.0f;
    };

    class BikeController {
    public:
        // 构造函数，注意这里使用的是全局的 UserState
        BikeController(JPH::PhysicsSystem* joltPhysics, InputSystem* input, UserState* state);
        ~BikeController() = default;

        void Init(uint32_t chassisBodyID);
        void Update(float dt);

        float get_steer_angle() const { return m_bicycle ? m_bicycle->steerAngle : 0.0f; }
        float get_speed() const { return m_bicycle ? m_bicycle->currentSpeed : 0.0f; }

    private:
        JPH::PhysicsSystem* m_joltPhysics = nullptr;
        InputSystem* m_inputSystem = nullptr;
        // 【关键修复】：必须在这里加上 m_state 的声明！
        UserState* m_state = nullptr;


        std::unique_ptr<BicycleState> m_bicycle;
    };

} // namespace engine