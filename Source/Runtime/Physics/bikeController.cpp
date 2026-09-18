#include "bikeController.hpp"
#include "BikeLateralGrip.hpp"
#include "../AudioSystem/AudioSystem.hpp"
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Body/BodyInterface.h>
#include <iostream>
#include <cmath>
#include <algorithm>
#include "../UserState/PlayerController.hpp"

#include "../Input/InputSystem.hpp" 
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Body/BodyFilter.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include "PhysicsSystem.hpp"
namespace engine
{

    BikeController::BikeController(JPH::PhysicsSystem* joltPhysics, InputSystem* input, PlayerController* player)
        : m_joltPhysics(joltPhysics), m_inputSystem(input), m_player(player)
    {
    }

    void BikeController::Init(uint32_t chassisBodyID)
    {
        if (!m_joltPhysics || !m_player || chassisBodyID == JPH::BodyID::cInvalidBodyID) return;

        m_bicycle = std::make_unique<BicycleState>();
        m_bicycle->chassisID = JPH::BodyID(chassisBodyID);

        JPH::BodyInterface& bi = m_joltPhysics->GetBodyInterface();
        bi.SetGravityFactor(m_bicycle->chassisID, 1.5f);
        m_motionResetRevision = m_player->State().motionResetRevision;
        m_engineForce = 0.0f;
        m_lastPedal = -1;
        SampleMotion();

        std::cout << "[Bicycle] bicycle created via BikeController." << std::endl;
    }

    void BikeController::SampleMotion() {
        if (!m_bicycle || !m_joltPhysics || !m_player) return;
        const auto revision = m_player->State().motionResetRevision;
        if (revision != m_motionResetRevision) {
            m_bicycle->steerAngle = 0.0f;
            m_bicycle->leanAngle = 0.0f;
            m_bicycle->currentSpeed = 0.0f;
            m_engineForce = 0.0f;
            m_lastPedal = -1;
            m_motionResetRevision = revision;
        }
        JPH::BodyInterface& bi = m_joltPhysics->GetBodyInterface();
        const JPH::BodyID id = m_bicycle->chassisID;
        if (!bi.IsAdded(id)) return;
        const JPH::Vec3 velocity = bi.GetLinearVelocity(id);
        const JPH::Vec3 forward = bi.GetRotation(id).RotateAxisZ();
        const float yaw = std::atan2(-forward.GetX(), -forward.GetZ());
        const float speedMps = std::sqrt(velocity.GetX() * velocity.GetX() + velocity.GetZ() * velocity.GetZ());
        m_bicycle->currentSpeed = velocity.GetX() * -std::sin(yaw) + velocity.GetZ() * -std::cos(yaw);
        const JPH::RVec3 position = bi.GetPosition(id);
        m_player->PublishMotion(speedMps, yaw, m_bicycle->steerAngle, m_bicycle->leanAngle,
            glm::vec3(static_cast<float>(position.GetX()), static_cast<float>(position.GetY()), static_cast<float>(position.GetZ())));
    }

    void BikeController::Update(float dt) {
        SampleMotion();
        if (!m_bicycle || !m_inputSystem || !m_joltPhysics || !m_player || !m_inputSystem->IsGameplayInputEnabled() || !m_player->CanControl() || dt <= 0.0f) return;

        JPH::BodyInterface& bi = m_joltPhysics->GetBodyInterface();
        JPH::BodyID id = m_bicycle->chassisID;
        if (!bi.IsAdded(id)) return;



        float inputThrottle = 0.0f;
        float inputSteer = 0.0f;

        if (m_inputSystem->IsGameplayActionHeld("MoveForward"))  inputThrottle += 1.0f;
        if (m_inputSystem->IsGameplayActionHeld("MoveBackward")) inputThrottle -= 1.0f;
        if (m_inputSystem->IsGameplayActionHeld("StrafeLeft"))   inputSteer += 1.0f;
        if (m_inputSystem->IsGameplayActionHeld("StrafeRight"))  inputSteer -= 1.0f;

        JPH::Quat currentRot = bi.GetRotation(id);
        JPH::Vec3 fwd = currentRot.RotateAxisZ();
        float currentYaw = std::atan2(-fwd.GetX(), -fwd.GetZ());

        JPH::Vec3 vel = bi.GetLinearVelocity(id);
        const float speed = std::sqrt(vel.GetX() * vel.GetX() + vel.GetZ() * vel.GetZ());

        float forwardX = -std::sin(currentYaw);
        float forwardZ = -std::cos(currentYaw);
        float signedSpeed = vel.GetX() * forwardX + vel.GetZ() * forwardZ;
        m_bicycle->currentSpeed = signedSpeed;

        JPH::RVec3 centerPos = bi.GetPosition(id);
        JPH::RRayCast ray{ centerPos, JPH::Vec3(0.0f, -1.8f, 0.0f) }; // Check 1.8 units below center of mass
        JPH::RayCastResult hit;
        JPH::IgnoreSingleBodyFilter bodyFilter(id);
        bool isGrounded = m_joltPhysics->GetNarrowPhaseQuery().CastRay(ray, hit, { }, { }, bodyFilter);

        if (m_player->State().jumpEnabled && isGrounded && m_inputSystem->IsGameplayActionPressed("Jump")) {
            vel.SetY(vel.GetY() + 16.0f); // Higher impulse to counteract the 3x gravity
            bi.SetLinearVelocity(id, vel);
            if (m_audio) m_audio->PlayOneShot("SpringJump");
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

        // �� BikeController::Update ��ǰ�벿����ȡ Pitch �Ƕ�
        JPH::Vec3 fwdLocalUnit(0.0f, 0.0f, -1.0f);
        JPH::Vec3 currentNose = currentRot * fwdLocalUnit;
        float currentPitch = std::asin(std::clamp(currentNose.GetY(), -1.0f, 1.0f));

        // ==========================================
        // ��������߼� (Loss of strength check)
        // ==========================================
        bool isLosingStrength = false;

        // Only treat near-stationary, heavily tipped bikes as a death state.
        if (std::abs(signedSpeed) < 2.0f && std::abs(currentPitch) > 0.85f && m_player->State().isAlive == true && isLosingStrength == false) {
            isLosingStrength = true;
            m_player->Die();
        }

        if (isLosingStrength) {
            // 1. �ָ������������ʵ���� (������Ȼ�����ٵز෭)
            // ֱ��ʹ�� Jolt ԭ����д�����޸ĵ�ǰ����
            {
                JPH::BodyLockWrite lock(m_joltPhysics->GetBodyLockInterface(), id);
                if (lock.Succeeded()) {
                    lock.GetBody().GetMotionProperties()->SetLinearDamping(0.05f);
                    lock.GetBody().GetMotionProperties()->SetAngularDamping(0.05f);
                }
            }

            // 2. Ϊ�˴��ƾ���ƽ�⣬����������ʩ��һ��΢С��������������
            if (bi.GetAngularVelocity(id).LengthSq() < 0.1f) {
                JPH::Vec3 rightDir = currentRot.RotateAxisX();
                bi.AddImpulse(id, rightDir * 0.5f); // ������һ�²���
            }

            // 3. �ؼ���ֱ�� return������ִ���·��κ� SetRotation �� SetAngularVelocity �Ĵ���
            // �� Jolt Physics ��ȫ�ӹ�������෭����������
            return;
        }
        else {
            // �ָ�����ʱ�ĸ����� (��ֹƽʱ�鴤)
            {
                JPH::BodyLockWrite lock(m_joltPhysics->GetBodyLockInterface(), id);
                if (lock.Succeeded()) {
                    lock.GetBody().GetMotionProperties()->SetLinearDamping(1.0f);
                    lock.GetBody().GetMotionProperties()->SetAngularDamping(10.0f);
                }
				isLosingStrength = true; // ֻ��Ҫ����һ�Σ�����ÿ֡���ᱣ�ָ����ᣬֱ���ٴδ�������
            }
        }
        // ... �·�����������ԭ�еĿ����߼� (SetRotation ��) ...




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
        float inverseMass = 0.0f;
        {
            JPH::BodyLockRead lock(m_joltPhysics->GetBodyLockInterface(), id);
            if (lock.Succeeded() && lock.GetBody().IsDynamic()) {
                inverseMass = lock.GetBody().GetMotionProperties()->GetInverseMass();
            }
        }
        const float lateralImpulse = CalculateBikeLateralGripImpulse(lateralSpeed, inverseMass, dt);
        if (lateralImpulse != 0.0f) {
            bi.AddImpulse(id, rightDirJPH * lateralImpulse);
        }

        // =========================================================
        // �������淨�������ҽ�������� (Pedal Mashing Mechanic)
        // =========================================================
        //static float s_engineForce = 0.0f;
        //// ��¼��һ�βȵ����ĸ�̤�� (-1: û��, 0: ��̤��, 1: ��̤��)
        //// (ע: �����Ϸ���ж����������������������Ƶ� BicycleState �ṹ����)
        //static int s_lastPedal = -1;
        bool justPedaled = false;

        if (m_inputSystem->IsGameplayActionPressed("pedal0")) {
            if (m_lastPedal != 0) {
                m_lastPedal = 0;
                justPedaled = true;
            }
        }
        if (m_inputSystem->IsGameplayActionPressed("pedal1")) {
            if (m_lastPedal != 1) {
                m_lastPedal = 1;
                justPedaled = true;
            }
        }
       

        // 2. ���������������趨 (���ݿ���̨)
        float targetMaxForce = 3000.0f + (speed * 20.0f); // �ٶ�Խ�죬�ܴﵽ�ļ�������Խ��
        float pedalBurstForce = 800.0f; // ���ؼ�������ÿ���һ����꣬�����ı�������
        float forceDecayRate = 500.0f;  // ���ؼ�������������ʧ�ٶȣ������������Ҫ�������ά���ٶȣ�

        // �����¶ȳͷ���������ԭ�����߼������¸�������
        float slopePenalty = 1.0f;
        if (currentPitch > 0.2f) {
            slopePenalty = std::max(0.0f, 1.0f - (currentPitch - 0.2f) * 2.5f);
        }

        // 3. ��������ע����˥��
        if (justPedaled) {
            m_engineForce += pedalBurstForce * slopePenalty;
            if (m_engineForce > targetMaxForce) {
                m_engineForce = targetMaxForce;
            }
        }
        else {
            if (m_engineForce > 0.0f) {
                m_engineForce -= forceDecayRate * dt;
                if (m_engineForce < 0.0f) m_engineForce = 0.0f;
            }
        }

        if (m_inputSystem->IsGameplayActionHeld("MoveBackward")) {
            m_engineForce -= 10000.0f * dt;
            if (m_engineForce < -500.0f) m_engineForce = -500.0f;
        }

        if (std::abs(m_engineForce) > 10.0f) {
            // �����ġ�������ҲҪ�ĳ� m_engineForce
            bi.AddForce(id, moveDirJPH * m_engineForce);
        }

        if (speed > 0.1f) {
            const float rollingFriction = 3.0f;
            float forceDir = signedSpeed > 0 ? -1.0f : 1.0f;
            bi.AddForce(id, moveDirJPH * (speed * rollingFriction * forceDir));
        }


        JPH::Vec3 angVel = bi.GetAngularVelocity(id);
        JPH::Vec3 localX = finalRot.RotateAxisX();
        float pitchAngVel = angVel.Dot(localX);
        bi.SetAngularVelocity(id, localX * (pitchAngVel * 0.85f));

        SampleMotion();
    }

} // namespace engine
