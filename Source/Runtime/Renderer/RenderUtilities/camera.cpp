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

	if (std::abs(dx) > 0.0f || std::abs(dy) > 0.0f)
	{
		if (aState.thirdPersonMode)
		{
			aState.Yaw -= dx;
			aState.Pitch += dy; 

			//limitation for pitch to prevent flipping
			float const max_pitch = glm::radians(85.0f);
			aState.Pitch = glm::clamp(aState.Pitch, -max_pitch, max_pitch);
		}
		else
		{
			auto const pos = cam[3];
			cam[3] = glm::vec4(0.f, 0.f, 0.f, 1.f);
			cam = glm::rotate(-dx, glm::vec3(0.f, 1.f, 0.f)) * cam;
			cam[3] = pos;
			cam = glm::rotate(cam, -dy, glm::vec3(1.f, 0.f, 0.f));
		}
	}

	// ==============================================================
	// Capture scroll wheel input, update and limit camera distance
	// If hovering the viewport OR controlling the camera (mouse captured), allow zoom
	if (aState.isSceneViewportHovered || inputSys->IsMouseCaptured()) {
		float scroll = inputSys->GetScrollY();
		if (scroll != 0.0f) {
			float zoomSpeed = 1.5f; // 缩放灵敏度，如果觉得太快或太慢可以改这里
			aState.Distance -= scroll * zoomSpeed; // 减去 scroll，因为通常向上滚是放大(拉近)

			// 限制相机的最小和最大距离：最近 1.5 米，最远 30 米
			aState.Distance = std::clamp(aState.Distance, 1.5f, 30.0f);
		}
	}
	// ==============================================================

	if (aState.thirdPersonMode)
	{
		glm::vec3 char_pos = aState.followTargetPos;
		glm::vec3 eye_offset(0.f, 1.6f, 0.f);
		glm::vec3 target_pos = char_pos + eye_offset;

		// 【关键修复 1】：删除原来的硬编码
		// float const distance = 5.0f; 

		// 【关键修复 2】：做个安全保护，如果 Distance 初始值为 0，给个默认值 5.0f
		if (aState.Distance < 1.0f) {
			aState.Distance = 5.0f;
		}

		// Yaw and Pitch to Cartesian coordinates
		glm::vec3 offset;
		// 【关键修复 3】：把原来的 distance 替换成 aState.Distance
		offset.x = aState.Distance * std::cos(aState.Pitch) * std::sin(aState.Yaw);
		offset.y = aState.Distance * std::sin(aState.Pitch);
		offset.z = aState.Distance * std::cos(aState.Pitch) * std::cos(aState.Yaw);

		glm::vec3 cam_pos = target_pos + offset;

		//World to View Matrix
		glm::mat4 view_matrix = glm::lookAt(cam_pos, target_pos, glm::vec3(0.f, 1.f, 0.f));
		cam = glm::inverse(view_matrix);
	}
	else
	{
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
	float const fov = lut::Radians(cfg::kCameraFov).value();

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
