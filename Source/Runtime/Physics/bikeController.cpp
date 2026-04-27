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
        bi.SetGravityFactor(m_bicycle->chassisID, 1.5f);

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

		// ==============================================================
		// 1. 获取物理姿态 (全面拥抱 +Z 作为正前方)
		// ==============================================================
		JPH::Quat currentRot = bi.GetRotation(id);

		// 【核心重构】：顺应你的 3D 模型，正前方就是 +Z！
		JPH::Vec3 fwdLocalUnit(0.0f, 0.0f, 1.0f);
		JPH::Vec3 physFwd = currentRot * fwdLocalUnit;

		// 计算 Yaw (直接使用原生的 atan2，没有任何负号修饰)
		float currentYaw = std::atan2(physFwd.GetX(), physFwd.GetZ());

		JPH::Vec3 vel = bi.GetLinearVelocity(id);
		speed = std::sqrt(vel.GetX() * vel.GetX() + vel.GetZ() * vel.GetZ());

		// 纯正的三角函数推导
		float forwardX = std::sin(currentYaw);
		float forwardZ = std::cos(currentYaw);

		// 点乘判断真实的前进/后退
		float dotProduct = vel.GetX() * physFwd.GetX() + vel.GetZ() * physFwd.GetZ();
		float signedSpeed = (dotProduct >= 0.0f) ? speed : -speed;
		m_bicycle->currentSpeed = signedSpeed;

		JPH::RVec3 centerPos = bi.GetPosition(id);
		JPH::RRayCast ray{ centerPos, JPH::Vec3(0.0f, -1.8f, 0.0f) };
		// ... 接下来的 RayCast 和 Jump 逻辑保持不变 ...
		JPH::RayCastResult hit;
		JPH::IgnoreSingleBodyFilter bodyFilter(id);
		bool isGrounded = m_joltPhysics->GetNarrowPhaseQuery().CastRay(ray, hit, { }, { }, bodyFilter);

		// 【关键修复 1】：离开地面后，切断油门和转向输入！
		if (!isGrounded) {
			inputThrottle = 0.0f;
			inputSteer = 0.0f;
		}

		if (isGrounded && m_inputSystem->IsActionPressed("Jump")) {
			vel.SetY(vel.GetY() + 16.0f);
			bi.SetLinearVelocity(id, vel);
		}

		// ==============================================================
		// 1.5 提取真实 Pitch
		// ==============================================================
		JPH::Vec3 currentNose = currentRot * fwdLocalUnit;
		float currentPitch = std::asin(std::clamp(currentNose.GetY(), -1.0f, 1.0f));

		// ==============================================================
		// 2. 转向与压弯 (恢复最纯净的输入映射)
		// ==============================================================
		float leanBlend = glm::clamp((speed - 5.0f) / 30.0f, 0.0f, 1.0f);
		float steerBlend = 1.0f - leanBlend;

		const float maxSteerAngle = glm::radians(45.0f);
		const float steerSpeed = glm::radians(150.0f);

		// A 键 (+1.0) -> 正角度 (左转)
		float targetSteer = inputSteer * maxSteerAngle * steerBlend;
		float steerDiff = targetSteer - m_bicycle->steerAngle;
		m_bicycle->steerAngle += glm::clamp(steerDiff, -steerSpeed * dt, steerSpeed * dt);

		const float maxLeanAngle = glm::radians(40.0f);
		const float leanSpeed = glm::radians(90.0f);

		// A 键 (+1.0) -> 负角度 (向左压弯)
		float targetLean = -inputSteer * maxLeanAngle * leanBlend;
		float leanDiff = targetLean - m_bicycle->leanAngle;
		m_bicycle->leanAngle += glm::clamp(leanDiff, -leanSpeed * dt, leanSpeed * dt);

		const float wheelBase = 1.6f;
		float steerYawRate = 0.0f;
		if (std::abs(signedSpeed) > 0.1f) {
			steerYawRate = (signedSpeed * std::tan(m_bicycle->steerAngle)) / wheelBase;
		}
		float leanYawRate = -m_bicycle->leanAngle * 1.5f * leanBlend;
		float newYaw = currentYaw + (steerYawRate + leanYawRate) * dt;

		// ==============================================================
		// 3. 干净的矩阵组装 (无 PI，无翻转)
		// ==============================================================
		JPH::Quat yawQuat = JPH::Quat::sRotation(JPH::Vec3::sAxisY(), newYaw);

		// 【关键修复】：由于朝向是 +Z，车头向上抬起时，绕 X 轴是负向旋转
		JPH::Quat pitchQuat = JPH::Quat::sRotation(JPH::Vec3::sAxisX(), -currentPitch);
		JPH::Quat leanQuat = JPH::Quat::sRotation(JPH::Vec3::sAxisZ(), m_bicycle->leanAngle);

		JPH::Quat finalRot = yawQuat * pitchQuat * leanQuat;
		bi.SetRotation(id, finalRot, JPH::EActivation::Activate);
		// ==========================================================
		// 4. 抓地力与推力矢量
		// ==========================================================
		const float maxSpeed = 40.0f;
		float slipAngle = m_bicycle->steerAngle * 0.5f;
		if (signedSpeed < 0.0f) slipAngle = -slipAngle;
		float moveYaw = newYaw + slipAngle;

		JPH::Quat moveYawQuat = JPH::Quat::sRotation(JPH::Vec3::sAxisY(), moveYaw);
		JPH::Quat noLeanRot = moveYawQuat * pitchQuat;

		// 顺着模型 +Z 完美向前的推力向量
		JPH::Vec3 forceDir = noLeanRot * fwdLocalUnit;

		// 【关键修复 2】：只有在地面上，才允许强制修改水平速度！
		// 如果在空中，绝对不能干涉，让重力带着它飞完美的抛物线！
		if (isGrounded && speed > 0.1f) {
			if (speed > maxSpeed) speed = maxSpeed;
			float driveSpeed = signedSpeed; // 前进为正，后退为负

			bi.SetLinearVelocity(id, JPH::Vec3(
				forceDir.GetX() * driveSpeed,
				bi.GetLinearVelocity(id).GetY(),
				forceDir.GetZ() * driveSpeed
			));
		}
		// (引擎推力 s_engineForce 的计算逻辑保持原样)
		static float s_engineForce = 0.0f;
		float targetMaxForce = 3000.0f + (speed * 30.0f);

		if (inputThrottle > 0.1f) {
			float speedRatio = glm::clamp(speed / maxSpeed, 0.0f, 1.0f);
			float inverseSpeed = 1.0f - speedRatio;
			float curveFactor = inverseSpeed * inverseSpeed * inverseSpeed * inverseSpeed * inverseSpeed;

			static float currentBoost = 0.0f;
			if (m_inputSystem->IsActionHeld("Fast")) {
				currentBoost += 2.0f * dt;
				if (currentBoost > 3.0f) currentBoost = 3.0f;
			}
			else {
				currentBoost -= 3.0f * dt;
				if (currentBoost < 0.0f) currentBoost = 0.0f;
			}

			float basicAccelRate = 100.0f + (150.0f * currentBoost);
			float burstAccelRate = (4000.0f + 1500.0f * currentBoost) * curveFactor;
			float currentAccelRate = basicAccelRate + burstAccelRate;

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

		// 将引擎推力沿斜坡方向施加！
		// 【关键修复 3】：只有轮胎挨着地，引擎和地面的摩擦力才能生效！
		if (isGrounded) {
			// 将引擎推力沿斜坡方向施加！
			if (std::abs(s_engineForce) > 10.0f) {
				bi.AddForce(id, forceDir * s_engineForce);
			}

			// 滚动阻力
			if (speed > 0.1f) {
				const float rollingFriction = 3.0f;
				float dirSign = signedSpeed > 0 ? -1.0f : 1.0f;
				bi.AddForce(id, forceDir * (speed * rollingFriction * dirSign));
			}
		}

		
		// 稳定动态物理，强行阻尼角速度
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