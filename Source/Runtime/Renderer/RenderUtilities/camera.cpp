#include "camera.hpp"

#include <glm/gtx/transform.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <cstdio>
#include "setup.hpp"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include "camera.hpp"
#include <glm/gtx/transform.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/matrix_inverse.hpp> 
#include <cmath>    
#include <algorithm> 
#include <limits>    
#include <glm/gtc/matrix_transform.hpp>
#include "light.hpp"
#include <imgui.h>



namespace lut = labut2;

#include "../../Input/InputSystem.hpp"

// Removed callbacks, now fully handled by engine::InputSystem
auto const mouseSens = cfg::kCameraMouseSensitivity;
void update_user_state(UserState& aState, float aElapsedTime, engine::InputSystem* inputSys)
{
	auto& cam = aState.camera2world;

	if (inputSys->IsActionPressed("CaptureMouse"))
	{
		aState.previousMouseState = !aState.previousMouseState;
		inputSys->SetMouseCaptured(aState.previousMouseState);
	}

	glm::vec2 mouseDelta(0.0f);
	if (aState.previousMouseState)
	{
		mouseDelta = inputSys->GetMouseDelta();
	}

	glm::vec2 gamepadDelta = inputSys->GetGamepadRightStick();
	// ==============================================================

	
	//auto const mouseSens = cfg::kCameraMouseSensitivity;
	
	float const gamepadSens = 2.5f * aElapsedTime;

	auto const dx = (mouseDelta.x * mouseSens) + (gamepadDelta.x * gamepadSens);
	auto const dy = (mouseDelta.y * mouseSens) + (gamepadDelta.y * gamepadSens);

	// ==============================================================
	// FOV 
	// ==============================================================
	float fovZoomSpeed = 40.0f; // rad per second
	if (inputSys->IsActionHeld("ZoomIn")) {
		aState.cameraFov -= fovZoomSpeed * aElapsedTime;
	}
	if (inputSys->IsActionHeld("ZoomOut")) {
		aState.cameraFov += fovZoomSpeed * aElapsedTime;
	}

	
	
	aState.cameraFov = std::clamp(aState.cameraFov, 10.0f, 120.0f);

	// ==============================================================
	// third person and first person
	// ==============================================================
	if (aState.thirdPersonMode)
	{
		// ==============================================================
		// 2. 滚轮控制：Distance 与 FOV 的完美数学映射 (Dolly Zoom)
		// ==============================================================
		if (aState.isSceneViewportHovered || inputSys->IsMouseCaptured()) {
			float scroll = inputSys->GetScrollY();
			if (scroll != 0.0f) {
				// A. 滚轮只负责更新“目标距离 (targetDistance)”
				float zoomSpeed = 9.0f;
				aState.targetDistance -= scroll * zoomSpeed;
				aState.targetDistance = std::clamp(aState.targetDistance, 0.5f, 70.0f);

				// B.出当前距离在 [0.5, 70.0] 区间内的百分比 (0.0 到 1.0)
				float distRatio = (aState.targetDistance - 0.5f) / (70.0f - 0.5f);

				// C. 强制绑定 FOV：距离最近(Ratio=0)时 FOV 为 120，最远(Ratio=1)时 FOV 为 20
				aState.targetFov = 120.0f - distRatio * (120.0f - 20.0f);
			}
		}
	
		// 确保 FOV 不会越界
		aState.targetFov = std::clamp(aState.targetFov, 10.0f, 120.0f);
		// 1. 获取输入，只更新 Target (目标值)
		/*if (aState.isSceneViewportHovered || inputSys->IsMouseCaptured()) {
			float scroll = inputSys->GetScrollY();
			if (scroll != 0.0f) {
				float zoomSpeed = 0.0f;
				aState.targetDistance -= scroll * zoomSpeed;
				aState.targetDistance = std::clamp(aState.targetDistance, 0.5f, 70.0f);
			}
		}*/

		// ==============================================================
		// 相机输入判定与自动回正逻辑
		// ==============================================================
		bool hasCameraInput = (std::abs(dx) > 0.001f || std::abs(dy) > 0.001f);

		if (hasCameraInput)
		{
			// 如果玩家正在转动视角，重置发呆计时器
			aState.cameraIdleTimer = 0.0f;

			aState.targetYaw -= dx;
			aState.targetPitch += dy;

			// 限制 Pitch 防止翻转
			float const max_pitch = glm::radians(85.0f);
			aState.targetPitch = glm::clamp(aState.targetPitch, -max_pitch, max_pitch);
		}
		else
		{
			// 如果玩家没碰鼠标
			aState.cameraIdleTimer += aElapsedTime;
		}

		// 自动回正触发条件：发呆超过 2.0 秒，且车速大于 2.0
		float autoAlignDelay = 0.5f;
		float minAlignSpeed = 2.0f;

		if (aState.targetFov >=115)
		{
			aState.isExtremeSpeed = true;
			
		}
		else 
		{
			aState.isExtremeSpeed = false; 
		}

		


		// 【核心修改】：只要玩家发呆超时 OR 处于急速状态，就开始回正！
		if ((aState.cameraIdleTimer > autoAlignDelay || aState.isExtremeSpeed) && std::abs(aState.bikeSpeed) > minAlignSpeed) {

			// ==============================================================
			// 基于相机距离的衰减因子 (Distance Falloff)
			// ==============================================================
			float fullEffectDist = 2.0f;  // 小于 2 米时，100% 自动回正
			float noEffectDist = 20.0f;   // 大于 20 米时，0% 自动回正（完全不受影响）

			// 计算距离因子：距离越远，factor 越接近 0
			float distanceFactor = 1.0f - glm::clamp((aState.Distance - fullEffectDist) / (noEffectDist - fullEffectDist), 0.0f, 1.0f);

			// 只有当距离因子有意义时才执行回正（节省性能）
			if (distanceFactor > 0.001f) {
				// 计算当前相机目标偏航角与单车真实偏航角的差值
				float diff = aState.bikeYaw - aState.targetYaw;

				// 规范化到 [-PI, PI]
				while (diff > glm::pi<float>())  diff -= 2.0f * glm::pi<float>();
				while (diff < -glm::pi<float>()) diff += 2.0f * glm::pi<float>();

				// 基础回正速度
				float baseAlignSpeed = 3.0f;

				// 锁住车头，对抗玩家鼠标输入
				if (aState.isExtremeSpeed) {
					baseAlignSpeed = 8.0f;
				}

				float alignSpeed = baseAlignSpeed * distanceFactor;

				// 渐渐将相机的目标 Yaw 修正到车头方向
				aState.targetYaw += diff * alignSpeed * aElapsedTime;

				// 将俯仰角(Pitch)也拉回到适合驾驶的高度（向下看 15 度左右）
				float defaultPitch = glm::radians(15.0f);
				aState.targetPitch += (defaultPitch - aState.targetPitch) * alignSpeed * aElapsedTime;
			}
		}
		// ==============================================================
		// ==============================================================
		// 2. camera lerp
		float smoothness = 6.0f; // 调节这个值：越小越滑(比如8.0f)，越大越跟手(比如20.0f)

		aState.cameraFov += (aState.targetFov - aState.cameraFov) * smoothness * aElapsedTime;
		aState.Yaw += (aState.targetYaw - aState.Yaw) * smoothness * aElapsedTime;
		aState.Pitch += (aState.targetPitch - aState.Pitch) * smoothness * aElapsedTime;
		aState.Distance += (aState.targetDistance - aState.Distance) * smoothness * aElapsedTime;

		// 3. 计算最终的相机矩阵
		glm::vec3 char_pos = aState.followTargetPos;
		glm::vec3 eye_offset(0.f, 1.6f, 0.f);
		glm::vec3 target_pos = char_pos + eye_offset;

		glm::vec3 offset;
		offset.x = aState.Distance * std::cos(aState.Pitch) * std::sin(aState.Yaw);
		offset.y = aState.Distance * std::sin(aState.Pitch);
		offset.z = aState.Distance * std::cos(aState.Pitch) * std::cos(aState.Yaw);

		glm::vec3 cam_pos = target_pos + offset;
		glm::mat4 view_matrix = glm::lookAt(cam_pos, target_pos, glm::vec3(0.f, 1.f, 0.f));
		cam = glm::inverse(view_matrix);
	}
	else
	{
		// 第一人称漫游模式：直接旋转，无需延迟
		if (std::abs(dx) > 0.0f || std::abs(dy) > 0.0f)
		{
			auto const pos = cam[3];
			cam[3] = glm::vec4(0.f, 0.f, 0.f, 1.f);
			cam = glm::rotate(-dx, glm::vec3(0.f, 1.f, 0.f)) * cam;
			cam[3] = pos;
			cam = glm::rotate(cam, -dy, glm::vec3(1.f, 0.f, 0.f));
		}

		// 第一人称移动 (WASD)
		auto const move = aElapsedTime * cfg::kCameraBaseSpeed *
			(inputSys->IsActionHeld("Fast") ? cfg::kCameraFastMult : 1.f) *
			(inputSys->IsActionHeld("Slow") ? cfg::kCameraSlowMult : 1.f);

		if (inputSys->IsActionHeld("MoveForward"))
			cam = cam * glm::translate(glm::vec3(0.f, 0.f, -move));
		if (inputSys->IsActionHeld("MoveBackward"))
			cam = cam * glm::translate(glm::vec3(0.f, 0.f, +move));
		if (inputSys->IsActionHeld("StrafeLeft"))
			cam = cam * glm::translate(glm::vec3(-move, 0.f, 0.f));
		if (inputSys->IsActionHeld("StrafeRight"))
			cam = cam * glm::translate(glm::vec3(+move, 0.f, 0.f));
		if (inputSys->IsActionHeld("Upward"))
			cam = cam * glm::translate(glm::vec3(0.f, +move, 0.f));
		if (inputSys->IsActionHeld("Downward"))
			cam = cam * glm::translate(glm::vec3(0.f, -move, 0.f));
	}
}

void update_scene_uniforms(glsl::SceneUniform& aSceneUniforms, std::uint32_t aFramebufferWidth, std::uint32_t aFramebufferHeight, UserState const& aState)
{
	float const aspect = float(aFramebufferWidth) / float(aFramebufferHeight);
	// ==============================================================
	
	// float const fov = lut::Radians(cfg::kCameraFov).value(); // 删掉或注释这行
	float const fov = glm::radians(aState.cameraFov); // 使用动态 FOV 并转成弧度
	// ==============================================================
	// 1. 相机矩阵计算
	aSceneUniforms.projection = glm::perspectiveRH_ZO(fov, aspect, cfg::kCameraNear, cfg::kCameraFar);
	aSceneUniforms.projection[1][1] *= -1.f;
	aSceneUniforms.camera = glm::inverse(aState.camera2world);
	aSceneUniforms.projCam = aSceneUniforms.projection * aSceneUniforms.camera;
	aSceneUniforms.cameraPos = glm::vec4(aState.camera2world[3]);

	// 2. 灯光基础信息
	aSceneUniforms.lightPos = glm::vec4(-50.0f, 100.0f, -30.0f, 0.0f);
	aSceneUniforms.lightColor = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
	aSceneUniforms.renderMode = std::uint32_t(aState.renderMode);

	// 3. 调用搬迁后的灯光系统计算阴影 
	engine::ShadowData shadow = engine::compute_csm_matrices(
		glm::vec3(aSceneUniforms.lightPos),
		aSceneUniforms.camera,
		fov, aspect,
		cfg::kCameraNear, cfg::kCameraFar
	);

	// 4. 填充结果
	aSceneUniforms.cascadeSplits = shadow.cascadeSplits;
	for (int i = 0; i < 4; ++i) aSceneUniforms.lightVP[i] = shadow.lightVP[i];
}
