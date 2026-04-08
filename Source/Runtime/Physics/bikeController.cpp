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
         speed = std::sqrt(vel.GetX() * vel.GetX() + vel.GetZ() * vel.GetZ());
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
        // ==============================================================
        //  (Speed Blend Factor)
        // 假设 5.0f 以下完全靠车把，40.0f 以上完全靠压弯
        // the steering mechanics. 
        // In my design, steering is achieved through two methods: rotating the handlebars and leaning the vehicle body. I have implemented both, but they aren't quite polished yet.
        //My ideal results are :
        //      1. Handlebar Steering : This is characterized by a wide turning angle, but it causes a loss of speed.
        //      2.Leaning : This method results in no speed loss, but the turning angle is more restricted.
        //   Both methods should be applied simultaneously, but their influence should shift based on velocity : 
        // the slower the speed, the higher the contribution of handlebar steering; conversely, the higher the speed, the higher the contribution of leaning.
        // 
        // ==============================================================
		float leanBlend = glm::clamp((speed - 5.0f) / 30.0f, 0.0f, 1.0f);// 0.0f at 5.0f speed, 1.0f at 35.0f speed
        float steerBlend = 1.0f - leanBlend;

        // ==============================================================
        // 2. steering
        // ==============================================================
		const float maxSteerAngle = glm::radians(45.0f); // big turning angle for handlebar
        const float steerSpeed = glm::radians(150.0f);

		//faster you go, the less you can steer with the handlebar, but the more you can turn by leaning. At very high speeds, the handlebar is almost locked, and you have to rely on leaning to turn.
        float targetSteer = inputSteer * maxSteerAngle * steerBlend;
        float steerDiff = targetSteer - m_bicycle->steerAngle;
        float maxDelta = steerSpeed * dt;
        m_bicycle->steerAngle += glm::clamp(steerDiff, -maxDelta, maxDelta);

        // ==============================================================
        // 3. leaning   
        // ==============================================================
        const float maxLeanAngle = glm::radians(40.0f);
        const float leanSpeed = glm::radians(90.0f);
        float maxLeanDelta = leanSpeed * dt;

        // 速度越快，leanBlend 越大，压弯幅度越深
        float targetLean = -inputSteer * maxLeanAngle * leanBlend;
        float leanDiff = targetLean - m_bicycle->leanAngle;
        m_bicycle->leanAngle += glm::clamp(leanDiff, -maxLeanDelta, maxLeanDelta);

        // ==============================================================
        // 4. Yaw Rate Combined
        // ==============================================================
        const float wheelBase = 1.6f;

        // 4.1 来自握把的转向率 (几何转向)
        float steerYawRate = 0.0f;
        if (std::abs(signedSpeed) > 0.1f) {
            steerYawRate = (signedSpeed * std::tan(m_bicycle->steerAngle)) / wheelBase;
        }

        // 4.2 来自压弯的转向率 
        // 倾角越大，转向越快；系数 1.5f 控制压弯的敏锐度
        float leanYawRate = -m_bicycle->leanAngle * 1.5f * leanBlend;

        // combine
        float yawRate = steerYawRate + leanYawRate;
        float newYaw = currentYaw + yawRate * dt;

        JPH::Vec3 fwdLocalUnit(0.0f, 0.0f, -1.0f);
        JPH::Vec3 currentNose = currentRot * fwdLocalUnit;
        float currentPitch = std::asin(std::clamp(currentNose.GetY(), -1.0f, 1.0f));

        JPH::Quat yawQuat = JPH::Quat::sRotation(JPH::Vec3::sAxisY(), newYaw + JPH::JPH_PI);
        JPH::Quat leanQuat = JPH::Quat::sRotation(JPH::Vec3::sAxisZ(), m_bicycle->leanAngle);
        JPH::Quat pitchQuat = JPH::Quat::sRotation(JPH::Vec3::sAxisX(), currentPitch);

        JPH::Quat finalRot = yawQuat * leanQuat * pitchQuat;
        bi.SetRotation(id, finalRot, JPH::EActivation::Activate);
        // ==========================================================
        // 5. Grip and drive power combined 
        // ==========================================================
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

		// virtual force pool for engine acceleration and braking, with inertia and dynamic torque curve
        static float s_engineForce = 0.0f;

        float targetMaxForce = 3000.0f + (speed * 30.0f); // 最高挡位推力

        if (inputThrottle > 0.1f) {
            // ==============================================================
            //非线性扭矩曲线 Torque Curve
            // ==============================================================
            // 1. 计算当前速度占极速的比例 (0.0 到 1.0)
            float speedRatio = glm::clamp(speed / maxSpeed, 0.0f, 1.0f);

			// 2. 二次方衰减曲线：start with 1，smooth low in mid，drop to zero at max speed
            float curveFactor = 1.0f - 10 *(speedRatio * speedRatio);

            // 3. 动态计算当前的推力增加率 (AccelRate)
            // 起步时 (curveFactor=1)  6000.0f 
            // 极速时 (curveFactor=0) only 500.0f
			float basicAccelRate = 800.0f;
            if (m_inputSystem->IsActionHeld("Fast"))  basicAccelRate = 1600.0f;
            else basicAccelRate = 800.0f;
           
            float currentAccelRate = basicAccelRate + (800.0f * curveFactor);
            
            

            // 4. 应用动态增加率
            s_engineForce += currentAccelRate * dt;
            if (s_engineForce > targetMaxForce) s_engineForce = targetMaxForce;
        }
        else if (inputThrottle < -0.1f) {
            // 持续按下 S：推力迅速变为负数 (active brake ）
            s_engineForce -= 10000.0f * dt;
            if (s_engineForce < -2500.0f) s_engineForce = -2500.0f; // 最大倒车力
        }
        else {
            // 松开按键：发动机推力自然衰减，缓缓归零
			//natrual decay towards zero when no throttle input, simulating engine inertia
            if (s_engineForce > 0.0f) {
                s_engineForce -= 100.0f * dt; // 衰减速度
                if (s_engineForce < 0.0f) s_engineForce = 0.0f;
            }
            else if (s_engineForce < 0.0f) {
                s_engineForce += 3000.0f * dt;
                if (s_engineForce > 0.0f) s_engineForce = 0.0f;
            }
        }

        // 将缓冲后的发动机推力作用于单车
        if (std::abs(s_engineForce) > 10.0f) {
            bi.AddForce(id, JPH::Vec3(
                moveDir.x * s_engineForce,
                0.0f,
                moveDir.z * s_engineForce
            ));
        }

		// fractional rolling friction that increases with speed, simulating tire grip and air resistance, but only when moving
        if (speed > 0.1f) {
            const float rollingFriction = 3.0f; 
            float forceDir = signedSpeed > 0 ? -1.0f : 1.0f;
            bi.AddForce(id, JPH::Vec3(
                moveDir.x * speed * rollingFriction * forceDir,
                0.0f,
                moveDir.z * speed * rollingFriction * forceDir
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

        
        m_state->bikeYaw = newYaw;
    }

} // namespace engine