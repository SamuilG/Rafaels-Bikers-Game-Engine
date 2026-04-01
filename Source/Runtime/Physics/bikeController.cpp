#include "bikeController.hpp"
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Body/BodyInterface.h>
#include <iostream>
#include <cmath>
#include <algorithm>
#include "../UserState/UserState.hpp"


// ���������Ŀ��ʵ��·������������ͷ�ļ�
 #include "../Input/InputSystem.hpp" 


namespace engine 
    {

        BikeController::BikeController(JPH::PhysicsSystem* joltPhysics, InputSystem* input, UserState* state)
            : m_joltPhysics(joltPhysics), m_inputSystem(input), m_state(state)
        {}

  

    void BikeController::Init(uint32_t chassisBodyID) 
    {
        if (!m_joltPhysics || chassisBodyID == JPH::BodyID::cInvalidBodyID) return;

        m_bicycle = std::make_unique<BicycleState>();
        m_bicycle->chassisID = JPH::BodyID(chassisBodyID);

        JPH::BodyInterface& bi = m_joltPhysics->GetBodyInterface();
        //bi.SetGravityFactor(m_bicycle->chassisID, 1.0f);
        //UI system for bike===========
        // initialize gravity from UI-editable tuning data
        bi.SetGravityFactor(m_bicycle->chassisID, m_state ? m_state->bikeTuning.gravityFactor : 1.0f);

        std::cout << "[Bicycle] bicycle created via BikeController." << std::endl;
    }

    void BikeController::Update(float dt) {
        // ���ָ��͵����˳�ģʽ
        if (!m_bicycle || !m_inputSystem || !m_joltPhysics || !m_state || !m_state->thirdPersonMode) return;

        JPH::BodyInterface& bi = m_joltPhysics->GetBodyInterface();
        JPH::BodyID id = m_bicycle->chassisID;
        if (!bi.IsAdded(id)) return;

        //UI system for bike===============
        //  pull the latest bicycle tuning values from UserState every frame
        const BikeTuning& tuning = m_state->bikeTuning;
        bi.SetGravityFactor(id, tuning.gravityFactor);

        float inputThrottle = 0.0f;
        float inputSteer = 0.0f;

        if (m_inputSystem->IsActionHeld("MoveForward"))  inputThrottle += 1.0f;
        if (m_inputSystem->IsActionHeld("MoveBackward")) inputThrottle -= 1.0f;
        if (m_inputSystem->IsActionHeld("StrafeLeft"))   inputSteer += 1.0f;
        if (m_inputSystem->IsActionHeld("StrafeRight"))  inputSteer -= 1.0f;

        // ==========================================
        // 1. ����ת�����
        // ==========================================

		const float maxSteerAngle = glm::radians(tuning.maxSteerAngleDeg);//UI system for bike========
		const float steerSpeed = glm::radians(tuning.steerSpeedDeg);//UI system for bike========
        float targetSteer = inputSteer * maxSteerAngle;
        float steerDiff = targetSteer - m_bicycle->steerAngle;
        float maxDelta = steerSpeed * dt;
        m_bicycle->steerAngle += glm::clamp(steerDiff, -maxDelta, maxDelta);

        // ==========================================
        // 2. �ٶȻ�ȡ
        // ==========================================
        JPH::Vec3 vel = bi.GetLinearVelocity(id);
        float speed = std::sqrt(vel.GetX() * vel.GetX() + vel.GetZ() * vel.GetZ());
        m_bicycle->currentSpeed = speed;

        // ==========================================
        // 3. ������б����
        // ==========================================
		const float maxLeanAngle = glm::radians(tuning.maxLeanAngleDeg);//UI system for bike=====
		const float leanSpeed = glm::radians(tuning.leanSpeedDeg);//UI system for bike======
        float maxLeanDelta = leanSpeed * dt;

        float targetLean = 0.0f;
        if (speed > 0.5f) {
            targetLean = -inputSteer * maxLeanAngle;
        }
        else {
            targetLean = 0.0f;
        }

        float leanDiff = targetLean - m_bicycle->leanAngle;
        m_bicycle->leanAngle += glm::clamp(leanDiff, -maxLeanDelta, maxLeanDelta);

        // ==========================================
        // 4. ��תӦ�� (Yaw & Lean)
        // ==========================================
        JPH::Quat currentRot = bi.GetRotation(id);
        JPH::Vec3 fwd = currentRot.RotateAxisZ();
        float currentYaw = std::atan2(-fwd.GetX(), -fwd.GetZ());

        //const float wheelBase = 1.6f;
		const float wheelBase = std::max(tuning.wheelBase, 0.01f);//UI system for bike==
        float yawRate = 0.0f;
        if (speed > 0.1f) {
            yawRate = (speed * std::tan(m_bicycle->steerAngle)) / wheelBase;
        }
        float newYaw = currentYaw + yawRate * dt;

        JPH::Quat yawQuat = JPH::Quat::sRotation(JPH::Vec3::sAxisY(), newYaw + JPH::JPH_PI);
        JPH::Quat leanQuat = JPH::Quat::sRotation(JPH::Vec3::sAxisZ(), m_bicycle->leanAngle);
        // JPH::Quat finalRot = yawQuat * leanQuat;

        // preserve pitch (x-axis rotation) from current rotation, so it aligns with slopes
        float fwdY = -fwd.GetY();

        float pitch = std::asin(std::max(-1.0f, std::min(1.0f, fwdY)));
        // limit pitch to avoid flipping over backwards completely
        pitch = std::max(-1.3f, std::min(1.3f, pitch));
        
        JPH::Quat pitchQuat = JPH::Quat::sRotation(JPH::Vec3::sAxisX(), pitch);

        JPH::Quat finalRot = yawQuat * pitchQuat * leanQuat;

        bi.SetRotation(id, finalRot, JPH::EActivation::Activate);

        // ==========================================
        // 5. ʩ��ǰ����������ɲ����
        // ==========================================
       /* const float driveForce = 1000.0f;
        const float brakeForce = 20.0f;
        const float maxSpeed = 60.0f;*/
		//UI system for bike==========
        const float driveForce = std::max(tuning.driveForce, 0.0f);
        const float brakeForce = std::max(tuning.brakeForce, 0.0f);
        const float maxSpeed = std::max(tuning.maxSpeed, 0.1f);

        glm::vec3 forwardDir(-std::sin(newYaw), 0.0f, -std::cos(newYaw));

        if (std::abs(inputThrottle) > 0.01f) {
            if (speed < maxSpeed || inputThrottle < 0.0f) {
                bi.AddForce(id, JPH::Vec3(
                    forwardDir.x * driveForce * inputThrottle,
                    0.0f,
                    forwardDir.z * driveForce * inputThrottle
                ));
            }
        }
        else {
            bi.AddForce(id, JPH::Vec3(
                -vel.GetX() * brakeForce,
                0.0f,
                -vel.GetZ() * brakeForce
            ));
        }

        // ����������Ľ��ٶ�
        bi.SetAngularVelocity(id, JPH::Vec3::sZero());

        // ǿ����������ٶ�
        if (speed > maxSpeed) {
            float scale = maxSpeed / speed;
            bi.SetLinearVelocity(id, JPH::Vec3(
                vel.GetX() * scale, vel.GetY(), vel.GetZ() * scale
            ));
        }
        m_state->bikeSpeed = speed;
        m_state->bikeSteerAngle = m_bicycle->steerAngle;
    }

} // namespace engine