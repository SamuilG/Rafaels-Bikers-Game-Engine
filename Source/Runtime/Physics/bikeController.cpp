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
#include "PhysicsSystem.hpp"
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

        // 在 BikeController::Update 的前半部分提取 Pitch 角度
        JPH::Vec3 fwdLocalUnit(0.0f, 0.0f, -1.0f);
        JPH::Vec3 currentNose = currentRot * fwdLocalUnit;
        float currentPitch = std::asin(std::clamp(currentNose.GetY(), -1.0f, 1.0f));

        // ==========================================
        // 脱力检测逻辑 (Loss of strength check)
        // ==========================================
        bool isLosingStrength = false;

        // 如果速度极低 (例如小于 0.5) 且 处于明显斜坡 (例如坡度大于 10度 / 0.17弧度)
        if (std::abs(signedSpeed) < 3.5f && std::abs(currentPitch) > 0.17f && m_state->isAlive == true && isLosingStrength == false) {
            isLosingStrength = true;
            m_state->isAlive = false; // 触发死亡/脱力状态
			m_state->deathTimer = 0.0f; // 重置死亡计时器
			m_state->thirdPersonMode = false; // 切换到第一人称视角
        }

        if (isLosingStrength) {
            // 1. 恢复物理引擎的真实阻尼 (让其自然、快速地侧翻)
            // 直接使用 Jolt 原生的写锁来修改当前刚体
            {
                JPH::BodyLockWrite lock(m_joltPhysics->GetBodyLockInterface(), id);
                if (lock.Succeeded()) {
                    lock.GetBody().GetMotionProperties()->SetLinearDamping(0.05f);
                    lock.GetBody().GetMotionProperties()->SetAngularDamping(0.05f);
                }
            }

            // 2. 为了打破绝对平衡，给单车侧面施加一个微小的推力让它倒下
            if (bi.GetAngularVelocity(id).LengthSq() < 0.1f) {
                JPH::Vec3 rightDir = currentRot.RotateAxisX();
                bi.AddImpulse(id, rightDir * 0.5f); // 轻轻推一下侧面
            }

            // 3. 关键：直接 return！不再执行下方任何 SetRotation 和 SetAngularVelocity 的代码
            // 让 Jolt Physics 完全接管重力与侧翻的物理结算
            return;
        }
        else {
            // 恢复骑行时的高阻尼 (防止平时抽搐)
            {
                JPH::BodyLockWrite lock(m_joltPhysics->GetBodyLockInterface(), id);
                if (lock.Succeeded()) {
                    lock.GetBody().GetMotionProperties()->SetLinearDamping(1.0f);
                    lock.GetBody().GetMotionProperties()->SetAngularDamping(10.0f);
                }
				isLosingStrength = true; // 只需要设置一次，后续每帧都会保持高阻尼，直到再次触发脱力
            }
        }
        // ... 下方继续保留你原有的控制逻辑 (SetRotation 等) ...




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

        // =========================================================
        // 【新增玩法】：左右交替狂点鼠标 (Pedal Mashing Mechanic)
        // =========================================================
        static float s_engineForce = 0.0f;
        // 记录上一次踩的是哪个踏板 (-1: 没踩, 0: 左踏板, 1: 右踏板)
        // (注: 如果游戏里有多辆单车，建议把这个变量移到 BicycleState 结构体里)
        static int s_lastPedal = -1;

        bool justPedaled = false; // 这一帧是否成功踩下了踏板

        // 1. 捕捉鼠标点击 (注意：必须使用 IsActionPressed，确保点一下只触发一帧)
        if (m_inputSystem->IsActionPressed("pedal0")) {
            // 必须交替踩！如果上一次也是踩的左边，就无效（防止玩家只狂点鼠标左键）
            if (s_lastPedal != 0) {
                s_lastPedal = 0;
                justPedaled = true;
            }
        }
        if (m_inputSystem->IsActionPressed("pedal1")) {
            if (s_lastPedal != 1) {
                s_lastPedal = 1;
                justPedaled = true;
            }
        }

        // 2. 推力与阻力参数设定 (导演控制台)
        float targetMaxForce = 3000.0f + (speed * 20.0f); // 速度越快，能达到的极速上限越高
        float pedalBurstForce = 800.0f; // 【关键参数】每点击一次鼠标，产生的爆发推力
        float forceDecayRate = 500.0f;  // 【关键参数】推力流失速度（决定了玩家需要点多快才能维持速度）

        // 计算坡度惩罚（保持你原来的逻辑：上坡更费力）
        float slopePenalty = 1.0f;
        if (currentPitch > 0.2f) {
            slopePenalty = std::max(0.0f, 1.0f - (currentPitch - 0.2f) * 2.5f);
        }

        // 3. 处理力的注入与衰减
        if (justPedaled) {
            // 点击成功：瞬间注入爆发力
            s_engineForce += pedalBurstForce * slopePenalty;
            if (s_engineForce > targetMaxForce) {
                s_engineForce = targetMaxForce; // 封顶限制
            }
        }
        else {
            // 没有点击：力量随着时间流失
            if (s_engineForce > 0.0f) {
                s_engineForce -= forceDecayRate * dt;
                if (s_engineForce < 0.0f) s_engineForce = 0.0f;
            }
        }

        // 4. 刹车逻辑 (保留 S 键刹车)
        if (m_inputSystem->IsActionHeld("MoveBackward")) {
            s_engineForce -= 10000.0f * dt; // 强力刹车
            if (s_engineForce < -500.0f) s_engineForce = -500.0f; // 允许缓慢倒车
        }

        // 5. 将最终的推力施加给物理引擎
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
        bi.SetAngularVelocity(id, localX * (pitchAngVel * 0.85f));

        m_state->bikeSpeed = speed; 
        m_state->bikeSteerAngle = m_bicycle->steerAngle;
        m_state->bikeYaw = newYaw;
        m_state->bikeLeanAngle = m_bicycle->leanAngle;
    }

} // namespace engine