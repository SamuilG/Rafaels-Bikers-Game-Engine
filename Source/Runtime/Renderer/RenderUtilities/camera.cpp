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

	auto const mouseSens = cfg::kCameraMouseSensitivity;
	float const gamepadSens = 2.5f * aElapsedTime;

	auto const dx = (mouseDelta.x * mouseSens) + (gamepadDelta.x * gamepadSens);
	auto const dy = (mouseDelta.y * mouseSens) + (gamepadDelta.y * gamepadSens);

	// ==============================================================
	// FOV 动态缩放逻辑 (第一/第三人称通用)
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
	// 分支：第三人称视角 vs 第一人称漫游视角
	// ==============================================================
	if (aState.thirdPersonMode)
	{
		// 1. 获取输入，只更新 Target (目标值)
		if (aState.isSceneViewportHovered || inputSys->IsMouseCaptured()) {
			float scroll = inputSys->GetScrollY();
			if (scroll != 0.0f) {
				float zoomSpeed = 1.5f;
				aState.targetDistance -= scroll * zoomSpeed;
				aState.targetDistance = std::clamp(aState.targetDistance, 1.5f, 30.0f);
			}
		}

		if (std::abs(dx) > 0.0f || std::abs(dy) > 0.0f)
		{
			aState.targetYaw -= dx;
			aState.targetPitch += dy;

			// 限制 Pitch 防止翻转
			float const max_pitch = glm::radians(85.0f);
			aState.targetPitch = glm::clamp(aState.targetPitch, -max_pitch, max_pitch);
		}

		// 2. camera lerp
		float smoothness = 6.0f; // 调节这个值：越小越滑(比如8.0f)，越大越跟手(比如20.0f)
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
