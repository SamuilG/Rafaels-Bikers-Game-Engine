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
    {
    }



    void BikeController::Init(uint32_t chassisBodyID)
    {
        if (!m_joltPhysics || chassisBodyID == JPH::BodyID::cInvalidBodyID) return;

        m_bicycle = std::make_unique<BicycleState>();
        m_bicycle->chassisID = JPH::BodyID(chassisBodyID);

        JPH::BodyInterface& bi = m_joltPhysics->GetBodyInterface();
        bi.SetGravityFactor(m_bicycle->chassisID, 1.0f);

        std::cout << "[Bicycle] bicycle created via BikeController." << std::endl;
    }

    void BikeController::Update(float dt) {
        // ���ָ��͵����˳�ģʽ
        if (!m_bicycle || !m_inputSystem || !m_joltPhysics || !m_state || !m_state->thirdPersonMode) return;

        JPH::BodyInterface& bi = m_joltPhysics->GetBodyInterface();
        JPH::BodyID id = m_bicycle->chassisID;
        if (!bi.IsAdded(id)) return;

        float inputThrottle = 0.0f;
        float inputSteer = 0.0f;

        if (m_inputSystem->IsActionHeld("MoveForward"))  inputThrottle += 1.0f;
        if (m_inputSystem->IsActionHeld("MoveBackward")) inputThrottle -= 1.0f;
        if (m_inputSystem->IsActionHeld("StrafeLeft"))   inputSteer += 1.0f;
        if (m_inputSystem->IsActionHeld("StrafeRight"))  inputSteer -= 1.0f;

        // 1. ��ȡ��ǰ���ٶȴ�С
        JPH::Vec3 vel = bi.GetLinearVelocity(id);
        float speed = std::sqrt(vel.GetX() * vel.GetX() + vel.GetZ() * vel.GetZ());
        m_bicycle->currentSpeed = speed;

        // 2. ����ת����� (���ٶ�Լ������ԭ�ش�ת)
        const float baseMaxSteerAngle = glm::radians(25.0f);
        const float steerSpeed = glm::radians(90.0f);

        float speedFactor = glm::clamp(speed / 5.0f, 0.1f, 1.0f);
        float currentMaxSteerAngle = baseMaxSteerAngle * speedFactor;

        float targetSteer = inputSteer * currentMaxSteerAngle;
        float steerDiff = targetSteer - m_bicycle->steerAngle;
        float maxDelta = steerSpeed * dt;
        m_bicycle->steerAngle += glm::clamp(steerDiff, -maxDelta, maxDelta);

        // 3. ������б����
        const float maxLeanAngle = glm::radians(30.0f);
        const float leanSpeed = glm::radians(90.0f);
        float maxLeanDelta = leanSpeed * dt;

        float targetLean = 0.0f;
        if (speed > 1.0f) {
            targetLean = -inputSteer * maxLeanAngle * speedFactor;
        }
        float leanDiff = targetLean - m_bicycle->leanAngle;
        m_bicycle->leanAngle += glm::clamp(leanDiff, -maxLeanDelta, maxLeanDelta);

        // 4. ���㳵��ʵ�ʵ���ת (Yaw)
        JPH::Quat currentRot = bi.GetRotation(id);
        JPH::Vec3 fwd = currentRot.RotateAxisZ();
        float currentYaw = std::atan2(-fwd.GetX(), -fwd.GetZ());

        const float wheelBase = 1.6f;
        float yawRate = 0.0f;
        if (speed > 0.1f) {
            yawRate = (speed * std::tan(m_bicycle->steerAngle)) / wheelBase;
        }
        float newYaw = currentYaw + yawRate * dt;

        JPH::Quat yawQuat = JPH::Quat::sRotation(JPH::Vec3::sAxisY(), newYaw + JPH::JPH_PI);
        JPH::Quat leanQuat = JPH::Quat::sRotation(JPH::Vec3::sAxisZ(), m_bicycle->leanAngle);
        JPH::Quat finalRot = yawQuat * leanQuat;

        bi.SetRotation(id, finalRot, JPH::EActivation::Activate);

        // ==========================================================
        // ������ħ������ģ��ץ�����������໬��ǿ�и���ǰ���ƶ���
        // ==========================================================
        const float maxSpeed = 60.0f;

        // ������������� + 50%���հѳ��򡱡����������ƶ����ָ�������ʵ�����г���
        float moveYaw = newYaw + m_bicycle->steerAngle * 0.5f;
        glm::vec3 moveDir(-std::sin(moveYaw), 0.0f, -std::cos(moveYaw));

        if (speed > 0.1f) {
            // ����һ������ٶȣ���ֹ�������
            if (speed > maxSpeed) speed = maxSpeed;

            // ǿ�а�ԭ�еĹ����ٶȣ�������ͷǰ���Ĺ켣�ϣ�(����Y����������)
            bi.SetLinearVelocity(id, JPH::Vec3(
                moveDir.x * speed,
                vel.GetY(),
                moveDir.z * speed
            ));
        }

        // ==========================================================
        // 5. ʩ������/ɲ���� (ʹ�ö����� moveDir)
        // ==========================================================
        const float driveForce = 1000.0f;
        const float brakeForce = 20.0f;

        if (std::abs(inputThrottle) > 0.01f) {
            if (speed < maxSpeed || inputThrottle < 0.0f) {
                bi.AddForce(id, JPH::Vec3(
                    moveDir.x * driveForce * inputThrottle,
                    0.0f,
                    moveDir.z * driveForce * inputThrottle
                ));
            }
        }
        else {
            // û�в�����ʱ��ʹ���뵱ǰ�ٶȳ����ȵ�������ɲ��
            bi.AddForce(id, JPH::Vec3(
                -moveDir.x * speed * brakeForce,
                0.0f,
                -moveDir.z * speed * brakeForce
            ));
        }

        // ����������Ľ��ٶȣ���ֹ����ײʱ�����һ���ҹ���
        bi.SetAngularVelocity(id, JPH::Vec3::sZero());

        // ͬ��������ϵͳ
        m_state->bikeSpeed = speed;
        m_state->bikeSteerAngle = m_bicycle->steerAngle;
    }

} // namespace engine