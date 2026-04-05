#include "bikeController.hpp"
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Body/BodyInterface.h>
#include <iostream>
#include <cmath>
#include <algorithm>
#include "../UserState/UserState.hpp"


#include "../Input/InputSystem.hpp" 
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Body/BodyFilter.h>


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
        bi.SetGravityFactor(m_bicycle->chassisID, 3.0f);

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

        // 1. Get current attitude and velocity
        JPH::Quat currentRot = bi.GetRotation(id);
        JPH::Vec3 fwd = currentRot.RotateAxisZ();
        float currentYaw = std::atan2(-fwd.GetX(), -fwd.GetZ());

        JPH::Vec3 vel = bi.GetLinearVelocity(id);
        float speed = std::sqrt(vel.GetX() * vel.GetX() + vel.GetZ() * vel.GetZ());
        float forwardX = -std::sin(currentYaw);
        float forwardZ = -std::cos(currentYaw);
        float signedSpeed = vel.GetX() * forwardX + vel.GetZ() * forwardZ;
        m_bicycle->currentSpeed = signedSpeed;

        JPH::RVec3 centerPos = bi.GetPosition(id);
        JPH::RRayCast ray{centerPos, JPH::Vec3(0.0f, -1.8f, 0.0f)}; // Check 1.8 units below center of mass
        JPH::RayCastResult hit;
        JPH::IgnoreSingleBodyFilter bodyFilter(id);
        bool isGrounded = m_joltPhysics->GetNarrowPhaseQuery().CastRay(ray, hit, { }, { }, bodyFilter);

        if (isGrounded && m_inputSystem->IsActionPressed("Jump")) {
            vel.SetY(vel.GetY() + 16.0f); // Higher impulse to counteract the 3x gravity
            bi.SetLinearVelocity(id, vel);
        }

        // 2. Target turning angle
        const float baseMaxSteerAngle = glm::radians(25.0f);
        const float steerSpeed = glm::radians(90.0f);

        float speedFactor = glm::clamp(speed / 5.0f, 0.1f, 1.0f);
        float currentMaxSteerAngle = baseMaxSteerAngle * speedFactor;

        float targetSteer = inputSteer * currentMaxSteerAngle;
        float steerDiff = targetSteer - m_bicycle->steerAngle;
        float maxDelta = steerSpeed * dt;
        m_bicycle->steerAngle += glm::clamp(steerDiff, -maxDelta, maxDelta);

        // 3. Target tilt angle
        const float maxLeanAngle = glm::radians(30.0f);
        const float leanSpeed = glm::radians(90.0f);
        float maxLeanDelta = leanSpeed * dt;

        float targetLean = 0.0f;
        if (speed > 1.0f) {
            targetLean = -inputSteer * maxLeanAngle * speedFactor;
        }
        float leanDiff = targetLean - m_bicycle->leanAngle;
        m_bicycle->leanAngle += glm::clamp(leanDiff, -maxLeanDelta, maxLeanDelta);

        // 4. Calculate the vehicle's actual steering (Yaw) and attitude reconstruction
        const float wheelBase = 1.6f;
        float yawRate = 0.0f;
        if (std::abs(signedSpeed) > 0.1f) {
            yawRate = (signedSpeed * std::tan(m_bicycle->steerAngle)) / wheelBase;
        }
        float newYaw = currentYaw + yawRate * dt;

        JPH::Vec3 fwdLocalUnit(0.0f, 0.0f, -1.0f);
        JPH::Vec3 currentNose = currentRot * fwdLocalUnit;
        float currentPitch = std::asin(std::clamp(currentNose.GetY(), -1.0f, 1.0f));

        JPH::Quat yawQuat = JPH::Quat::sRotation(JPH::Vec3::sAxisY(), newYaw + JPH::JPH_PI);
        JPH::Quat leanQuat = JPH::Quat::sRotation(JPH::Vec3::sAxisZ(), m_bicycle->leanAngle);
        JPH::Quat pitchQuat = JPH::Quat::sRotation(JPH::Vec3::sAxisX(), currentPitch);

        JPH::Quat finalRot = yawQuat * leanQuat * pitchQuat;
        bi.SetRotation(id, finalRot, JPH::EActivation::Activate);

        // 5. Grip and drive power combined
        const float maxSpeed = 60.0f;
        float slipAngle = m_bicycle->steerAngle * 0.5f;
        if (signedSpeed < 0.0f) slipAngle = -slipAngle;
        float moveYaw = newYaw + slipAngle;
        glm::vec3 moveDir(-std::sin(moveYaw), 0.0f, -std::cos(moveYaw));

        if (speed > 0.1f) {
            if (speed > maxSpeed) speed = maxSpeed;
            float driveSpeed = signedSpeed > 0 ? speed : -speed;
            bi.SetLinearVelocity(id, JPH::Vec3(
                moveDir.x * driveSpeed,
                bi.GetLinearVelocity(id).GetY(),
                moveDir.z * driveSpeed
            ));
        }

        const float driveForce = 2000.0f;
        const float brakeForce = 40.0f;

        if (std::abs(inputThrottle) > 0.01f) {
            if (speed < maxSpeed || (inputThrottle > 0.0f && signedSpeed < 0.0f) || (inputThrottle < 0.0f && signedSpeed > 0.0f)) {
                bi.AddForce(id, JPH::Vec3(
                    moveDir.x * driveForce * inputThrottle,
                    0.0f,
                    moveDir.z * driveForce * inputThrottle
                ));
            }
        }
        else {
            float forceDir = signedSpeed > 0 ? -1.0f : 1.0f;
            bi.AddForce(id, JPH::Vec3(
                moveDir.x * speed * brakeForce * forceDir,
                0.0f,
                moveDir.z * speed * brakeForce * forceDir
            ));
        }

        // stabilise dynamic physics: Isolate and decay pitch rate, force roll and yaw rates to 0
        JPH::Vec3 angVel = bi.GetAngularVelocity(id);
        JPH::Vec3 localX = finalRot.RotateAxisX();
        float pitchAngVel = angVel.Dot(localX);
        bi.SetAngularVelocity(id, localX * (pitchAngVel * 0.85f));
        // 
        m_state->bikeSpeed = speed;
        m_state->bikeSteerAngle = m_bicycle->steerAngle;
    }

} // namespace engine