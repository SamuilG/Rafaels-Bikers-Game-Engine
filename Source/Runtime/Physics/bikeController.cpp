#include "bikeController.hpp"
#include "../AudioSystem/AudioSystem.hpp"
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
#include <Jolt/Physics/Body/BodyLock.h>

namespace engine
{
    float speed;

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
        bi.SetGravityFactor(m_bicycle->chassisID, 1.5f);

        std::cout << "[Bicycle] bicycle created via BikeController." << std::endl;
    }

    void BikeController::Update(float dt) {
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

        JPH::Quat currentRot = bi.GetRotation(id);
        JPH::Vec3 fwd = currentRot.RotateAxisZ();
        float currentYaw = std::atan2(-fwd.GetX(), -fwd.GetZ());

        JPH::Vec3 vel = bi.GetLinearVelocity(id);
        speed = std::sqrt(vel.GetX() * vel.GetX() + vel.GetZ() * vel.GetZ());

        float forwardX = -std::sin(currentYaw);
        float forwardZ = -std::cos(currentYaw);
        float signedSpeed = vel.GetX() * forwardX + vel.GetZ() * forwardZ;
        m_bicycle->currentSpeed = signedSpeed;

        JPH::RVec3 centerPos = bi.GetPosition(id);
        JPH::RRayCast ray{ centerPos, JPH::Vec3(0.0f, -1.8f, 0.0f) }; // Check 1.8 units below center of mass
        JPH::RayCastResult hit;
        JPH::IgnoreSingleBodyFilter bodyFilter(id);
        bool isGrounded = m_joltPhysics->GetNarrowPhaseQuery().CastRay(ray, hit, { }, { }, bodyFilter);

        if (isGrounded && m_inputSystem->IsActionPressed("Jump")) {
            vel.SetY(vel.GetY() + 16.0f); // Higher impulse to counteract the 3x gravity
            bi.SetLinearVelocity(id, vel);
            {
                //m_audio->LoadSound("Horn", "Assets/Sounds/bicycle_horn.mp3");
                //m_audio->SetVolume("Jump", 0.5f);
                //m_audio->PlayOneShot("Jump");
            }
        }

        float leanBlend = glm::clamp((speed - 5.0f) / 30.0f, 0.0f, 1.0f);
        float steerBlend = 1.0f - leanBlend;


        const float maxSteerAngle = glm::radians(45.0f);
        const float steerSpeed = glm::radians(150.0f);

        float targetSteer = inputSteer * maxSteerAngle * steerBlend;
        float steerDiff = targetSteer - m_bicycle->steerAngle;
        float maxDelta = steerSpeed * dt;
        m_bicycle->steerAngle += glm::clamp(steerDiff, -maxDelta, maxDelta);


        const float maxLeanAngle = glm::radians(40.0f);
        const float leanSpeed = glm::radians(90.0f);
        float maxLeanDelta = leanSpeed * dt;

        float targetLean = -inputSteer * maxLeanAngle * leanBlend;
        float leanDiff = targetLean - m_bicycle->leanAngle;
        m_bicycle->leanAngle += glm::clamp(leanDiff, -maxLeanDelta, maxLeanDelta);


        const float wheelBase = 1.6f;

        float steerYawRate = 0.0f;
        if (std::abs(signedSpeed) > 0.1f) {
            steerYawRate = (signedSpeed * std::tan(m_bicycle->steerAngle)) / wheelBase;
        }

        float leanYawRate = -m_bicycle->leanAngle * 1.5f * leanBlend;
        float yawRate = steerYawRate + leanYawRate;
        float newYaw = currentYaw + yawRate * dt;

        JPH::Vec3 fwdLocalUnit(0.0f, 0.0f, -1.0f);
        JPH::Vec3 currentNose = currentRot * fwdLocalUnit;
        float currentPitch = std::asin(std::clamp(currentNose.GetY(), -1.0f, 1.0f));

        JPH::Quat yawQuat = JPH::Quat::sRotation(JPH::Vec3::sAxisY(), newYaw + JPH::JPH_PI);
        JPH::Quat leanQuat = JPH::Quat::sRotation(JPH::Vec3::sAxisZ(), m_bicycle->leanAngle);
        JPH::Quat pitchQuat = JPH::Quat::sRotation(JPH::Vec3::sAxisX(), currentPitch);

        JPH::Quat finalRot = yawQuat * pitchQuat * leanQuat;
        bi.SetRotation(id, finalRot, JPH::EActivation::Activate);


        const float maxSpeed = 40.0f;
        float slipAngle = m_bicycle->steerAngle * 0.5f;
        if (signedSpeed < 0.0f) slipAngle = -slipAngle;
        float moveYaw = newYaw + slipAngle;

        JPH::Vec3 moveDirJPH(-std::sin(moveYaw), 0.0f, -std::cos(moveYaw)); 
        JPH::Vec3 rightDirJPH(std::cos(moveYaw), 0.0f, -std::sin(moveYaw));  

        JPH::Vec3 currentVel = bi.GetLinearVelocity(id);
        float lateralSpeed = currentVel.Dot(rightDirJPH);
        float lateralGripStiffness = 5000.0f; 
        bi.AddForce(id, rightDirJPH * (-lateralSpeed * lateralGripStiffness));

        static float s_engineForce = 0.0f;
        float targetMaxForce = 3000.0f + (speed * 30.0f);

        if (inputThrottle > 0.1f) {
            float speedRatio = glm::clamp(speed / maxSpeed, 0.0f, 1.0f);
            float inverseSpeed = 1.0f - speedRatio;
            float curveFactor = inverseSpeed * inverseSpeed * inverseSpeed * inverseSpeed * inverseSpeed;

            static float currentBoost = 0.0f;
            float boostRampUpSpeed = 2.0f;
            float boostRampDownSpeed = 3.0f;

            if (m_inputSystem->IsActionHeld("Fast")) {
                currentBoost += boostRampUpSpeed * dt;
                if (currentBoost > 3.0f) currentBoost = 3.0f;
            }
            else {
                currentBoost -= boostRampDownSpeed * dt;
                if (currentBoost < 0.0f) currentBoost = 0.0f;
            }

            float basicAccelRate = 100.0f + (150.0f * currentBoost);
            float burstAccelRate = (4000.0f + 1500.0f * currentBoost) * curveFactor;
            float currentAccelRate = basicAccelRate + burstAccelRate;

            // slopePenalty
            float slopePenalty = 1.0f;
            if (currentPitch > 0.2f) { 
                slopePenalty = std::max(0.0f, 1.0f - (currentPitch - 0.2f) * 2.5f);
            }
            currentAccelRate *= slopePenalty;

            s_engineForce += currentAccelRate * dt;
            if (s_engineForce > targetMaxForce) s_engineForce = targetMaxForce;
        }
        else if (inputThrottle < -0.1f) {
            s_engineForce -= 10000.0f * dt;
            if (s_engineForce < -2500.0f) s_engineForce = -2500.0f;
        }
        else {
            if (s_engineForce > 0.0f) {
                s_engineForce -= 100.0f * dt;
                if (s_engineForce < 0.0f) s_engineForce = 0.0f;
            }
            else if (s_engineForce < 0.0f) {
                s_engineForce += 3000.0f * dt;
                if (s_engineForce > 0.0f) s_engineForce = 0.0f;
            }
        }

        if (std::abs(s_engineForce) > 10.0f) {
            bi.AddForce(id, moveDirJPH * s_engineForce);
        }

        if (speed > 0.1f) {
            const float rollingFriction = 3.0f;
            float forceDir = signedSpeed > 0 ? -1.0f : 1.0f;
            bi.AddForce(id, moveDirJPH * (speed * rollingFriction * forceDir));
        }


        JPH::Vec3 angVel = bi.GetAngularVelocity(id);
        JPH::Vec3 localX = finalRot.RotateAxisX();
        float pitchAngVel = angVel.Dot(localX);
        bi.SetAngularVelocity(id, localX * (pitchAngVel ));

        m_state->bikeSpeed = speed; 
        m_state->bikeSteerAngle = m_bicycle->steerAngle;
        m_state->bikeYaw = newYaw;
        m_state->bikeLeanAngle = m_bicycle->leanAngle;
    }

} // namespace engine