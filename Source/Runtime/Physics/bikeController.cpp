#include "bikeController.hpp"
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Body/BodyInterface.h>
#include <iostream>
#include <cmath>
#include <algorithm>
#include "../UserState/UserState.hpp"


// 锟斤拷锟斤拷锟斤拷锟斤拷锟侥匡拷锟绞碉拷锟铰凤拷锟斤拷锟斤拷锟斤拷锟斤拷锟斤拷锟酵凤拷募锟?
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
        // 锟斤拷锟街革拷锟酵碉拷锟斤拷锟剿筹拷模式
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

<<<<<<< HEAD
        // 1. 获取当前线速度大小
=======
        // ==========================================
        // 1. 锟斤拷锟斤拷转锟斤拷锟斤拷锟?
        // ==========================================

		const float maxSteerAngle = glm::radians(tuning.maxSteerAngleDeg);//UI system for bike========
		const float steerSpeed = glm::radians(tuning.steerSpeedDeg);//UI system for bike========
        float targetSteer = inputSteer * maxSteerAngle;
        float steerDiff = targetSteer - m_bicycle->steerAngle;
        float maxDelta = steerSpeed * dt;
        m_bicycle->steerAngle += glm::clamp(steerDiff, -maxDelta, maxDelta);

        // ==========================================
        // 2. 锟劫度伙拷取
        // ==========================================
>>>>>>> c3258c92c17c2bf52e537b464e4b5a63e6448f70
        JPH::Vec3 vel = bi.GetLinearVelocity(id);
        float speed = std::sqrt(vel.GetX() * vel.GetX() + vel.GetZ() * vel.GetZ());
        m_bicycle->currentSpeed = speed;

<<<<<<< HEAD
        // 2. 车把转向计算 (带速度约束，防原地打转)
        const float baseMaxSteerAngle = glm::radians(25.0f);
        const float steerSpeed = glm::radians(90.0f);

        float speedFactor = glm::clamp(speed / 5.0f, 0.1f, 1.0f);
        float currentMaxSteerAngle = baseMaxSteerAngle * speedFactor;

        float targetSteer = inputSteer * currentMaxSteerAngle;
        float steerDiff = targetSteer - m_bicycle->steerAngle;
        float maxDelta = steerSpeed * dt;
        m_bicycle->steerAngle += glm::clamp(steerDiff, -maxDelta, maxDelta);

        // 3. 车身倾斜计算
        const float maxLeanAngle = glm::radians(30.0f);
        const float leanSpeed = glm::radians(90.0f);
=======
        // ==========================================
        // 3. 锟斤拷锟斤拷锟斤拷斜锟斤拷锟斤拷
        // ==========================================
		const float maxLeanAngle = glm::radians(tuning.maxLeanAngleDeg);//UI system for bike=====
		const float leanSpeed = glm::radians(tuning.leanSpeedDeg);//UI system for bike======
>>>>>>> c3258c92c17c2bf52e537b464e4b5a63e6448f70
        float maxLeanDelta = leanSpeed * dt;

        float targetLean = 0.0f;
        if (speed > 1.0f) {
            targetLean = -inputSteer * maxLeanAngle * speedFactor;
        }
        float leanDiff = targetLean - m_bicycle->leanAngle;
        m_bicycle->leanAngle += glm::clamp(leanDiff, -maxLeanDelta, maxLeanDelta);

<<<<<<< HEAD
        // 4. 计算车身实际的旋转 (Yaw)
=======
        // ==========================================
        // 4. 锟斤拷转应锟斤拷 (Yaw & Lean)
        // ==========================================
>>>>>>> c3258c92c17c2bf52e537b464e4b5a63e6448f70
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

<<<<<<< HEAD
        // ==========================================================
        // 【核心魔法】：模拟抓地力，消除侧滑，强行跟随前轮移动！
        // ==========================================================
        const float maxSpeed = 60.0f;
=======
        // ==========================================
        // 5. 施锟斤拷前锟斤拷锟斤拷锟斤拷锟斤拷锟斤拷刹锟斤拷锟斤拷
        // ==========================================
       /* const float driveForce = 1000.0f;
        const float brakeForce = 20.0f;
        const float maxSpeed = 60.0f;*/
		//UI system for bike==========
        const float driveForce = std::max(tuning.driveForce, 0.0f);
        const float brakeForce = std::max(tuning.brakeForce, 0.0f);
        const float maxSpeed = std::max(tuning.maxSpeed, 0.1f);
>>>>>>> c3258c92c17c2bf52e537b464e4b5a63e6448f70

        // 算出“车身朝向 + 50%的握把朝向”。这样重心移动的手感最像真实的自行车！
        float moveYaw = newYaw + m_bicycle->steerAngle * 0.5f;
        glm::vec3 moveDir(-std::sin(moveYaw), 0.0f, -std::cos(moveYaw));

        if (speed > 0.1f) {
            // 限制一下最大速度，防止下坡起飞
            if (speed > maxSpeed) speed = maxSpeed;

            // 强行把原有的惯性速度，掰到车头前方的轨迹上！(保留Y轴重力下落)
            bi.SetLinearVelocity(id, JPH::Vec3(
                moveDir.x * speed,
                vel.GetY(),
                moveDir.z * speed
            ));
        }

        // ==========================================================
        // 5. 施加推力/刹车力 (使用对齐后的 moveDir)
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
            // 没有踩油门时，使用与当前速度成正比的阻力来刹车
            bi.AddForce(id, JPH::Vec3(
                -moveDir.x * speed * brakeForce,
                0.0f,
                -moveDir.z * speed * brakeForce
            ));
        }

<<<<<<< HEAD
        // 锁定其他轴的角速度（防止在碰撞时像个球一样乱滚）
        bi.SetAngularVelocity(id, JPH::Vec3::sZero());

        // 同步给其他系统
=======
        // 锟斤拷锟斤拷锟斤拷锟斤拷锟斤拷慕锟斤拷俣锟?
        bi.SetAngularVelocity(id, JPH::Vec3::sZero());

        // 强锟斤拷锟斤拷锟斤拷锟斤拷锟斤拷俣锟?
        if (speed > maxSpeed) {
            float scale = maxSpeed / speed;
            bi.SetLinearVelocity(id, JPH::Vec3(
                vel.GetX() * scale, vel.GetY(), vel.GetZ() * scale
            ));
        }
>>>>>>> c3258c92c17c2bf52e537b464e4b5a63e6448f70
        m_state->bikeSpeed = speed;
        m_state->bikeSteerAngle = m_bicycle->steerAngle;
    }

} // namespace engine