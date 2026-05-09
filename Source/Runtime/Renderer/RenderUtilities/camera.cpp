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
#include "../../Input/InputSystem.hpp"

#include "../../UserState/UserState.hpp" // 请根据你的实际目录层级确认路径
namespace lut = labut2;

float extremeSpeedThreshold = 35.0f;
constexpr float kFixedGameplayFov = 85.0f;

namespace engine {
	StereoCameraFrame build_stereo_camera_frame(
		const engine::UserState& state,
		std::uint32_t framebufferWidth,
		std::uint32_t framebufferHeight,
		float ipdMeters)
	{
		StereoCameraFrame frame{};
		frame.enabled = true;
		frame.ipdMeters = ipdMeters;
		frame.aspectRatio = framebufferHeight > 0
			? float(framebufferWidth) / float(framebufferHeight)
			: 1.0f;

		const glm::mat4 centerWorldFromEye = state.camera.camera2world;
		const glm::vec3 centerPos = glm::vec3(centerWorldFromEye[3]);
		const glm::vec3 right = glm::normalize(glm::vec3(centerWorldFromEye[0]));
		const glm::vec3 eyeOffset = right * (ipdMeters * 0.5f);
		const float fovRadians = glm::radians(state.camera.cameraFov);
		const glm::mat4 projection = glm::perspectiveRH_ZO(
			fovRadians,
			frame.aspectRatio,
			cfg::kCameraNear,
			cfg::kCameraFar);

		auto buildEye = [&](StereoEyeIndex eye, const glm::vec3& worldPos) {
			StereoEyeView viewData{};
			viewData.eye = eye;
			viewData.worldFromEye = centerWorldFromEye;
			viewData.worldFromEye[3] = glm::vec4(worldPos, 1.0f);
			viewData.view = glm::inverse(viewData.worldFromEye);
			viewData.projection = projection;
			viewData.projection[1][1] *= -1.f;
			viewData.viewProjection = viewData.projection * viewData.view;
			viewData.worldPosition = worldPos;
			viewData.nearPlane = cfg::kCameraNear;
			viewData.farPlane = cfg::kCameraFar;
			viewData.verticalFovRadians = fovRadians;
			return viewData;
		};

		frame.left = buildEye(StereoEyeIndex::Left, centerPos - eyeOffset);
		frame.right = buildEye(StereoEyeIndex::Right, centerPos + eyeOffset);
		return frame;
	}
}

// Removed callbacks, now fully handled by engine::InputSystem
void update_user_state(engine::UserState& aState, float aElapsedTime, engine::InputSystem* inputSys)
{
	auto& camera = aState.camera;
	auto& gameplay = aState.gameplay;
	auto& cam = camera.camera2world;

	if (inputSys->IsActionPressed("CaptureMouse") && gameplay.isAlive)
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
	// FOV 手动微调
	// ==============================================================
	camera.targetFov = kFixedGameplayFov;
	camera.cameraFov = kFixedGameplayFov;

	// ==============================================================
	// third person and first person
	// ==============================================================
	if (aState.thirdPersonMode)
	{
		// 1. 首先评估状态！防止延迟一帧
		aState.isExtremeSpeed = false;

		// ==============================================================
		// 2. 状态机：基于真实物理速度的极速视角接管 (双重插值 Dolly Zoom)
		// ==============================================================
		if (aState.isExtremeSpeed) {
			// 【平滑接管】：无论目前滚轮在哪，都平滑过渡到极速视角
			float transitionSpeed = 7.5f;
			camera.targetDistance += (0.5f - camera.targetDistance) * transitionSpeed * aElapsedTime;
		}
		else {
			// 【平滑释放】：刚退出极速状态时，平滑弹回到正常下限 2.0f
			if (camera.targetDistance < 2.0f) {
				float recoverySpeed = 4.0f;
				camera.targetDistance += (2.0f - camera.targetDistance) * recoverySpeed * aElapsedTime;
			}

			// 【正常滚轮调节】
			if (aState.isSceneViewportHovered || inputSys->IsMouseCaptured()) {
				float scroll = inputSys->GetScrollY();
				if (scroll != 0.0f) {
					float zoomSpeed = 9.0f;
					camera.targetDistance -= scroll * zoomSpeed;

					// 【关键修复】：无条件死死钳制住！不再给它逃逸到 2.0 以下的机会
					camera.targetDistance = std::clamp(camera.targetDistance, 2.0f, 70.0f);
				}
			}

			// 重新映射百分比。使用 std::max 防止在平滑释放期间出现负数
			float safeDist = std::max(camera.targetDistance, 2.0f);

			// 映射 FOV。距离 2.0(Ratio=0) 时 FOV=100，距离 70(Ratio=1) 时 FOV=20
			camera.targetFov = kFixedGameplayFov;
		}

		// 确保目标 FOV 不会突破物理极值
		camera.targetFov = kFixedGameplayFov;
	 
		// ==============================================================
		// 3. 相机输入判定与自动回正逻辑
		// ==============================================================
		bool hasCameraInput = (std::abs(dx) > 0.001f || std::abs(dy) > 0.001f);

		if (hasCameraInput)
		{
			camera.cameraIdleTimer = 0.0f;
			camera.targetYaw -= dx;
			camera.targetPitch += dy;

			float const max_pitch = glm::radians(85.0f);
			camera.targetPitch = glm::clamp(camera.targetPitch, -max_pitch, max_pitch);
		}
		else
		{
			camera.cameraIdleTimer += aElapsedTime;
		}

		float autoAlignDelay = 0.5f;
		float minAlignSpeed = 2.0f;

		if ((camera.cameraIdleTimer > autoAlignDelay || aState.isExtremeSpeed) && std::abs(aState.bikeSpeed) > minAlignSpeed) {

			float fullEffectDist = 2.0f;
			float noEffectDist = 20.0f;
			float distanceFactor = 1.0f - glm::clamp((camera.Distance - fullEffectDist) / (noEffectDist - fullEffectDist), 0.0f, 1.0f);

			if (distanceFactor > 0.001f) {
				float diff = camera.bikeYaw - camera.targetYaw;

				while (diff > glm::pi<float>())  diff -= 2.0f * glm::pi<float>();
				while (diff < -glm::pi<float>()) diff += 2.0f * glm::pi<float>();

				float baseAlignSpeed = 3.0f;
				if (aState.isExtremeSpeed) {
					baseAlignSpeed = 8.0f;
				}

				float alignSpeed = baseAlignSpeed * distanceFactor;

				camera.targetYaw += diff * alignSpeed * aElapsedTime;

				float defaultPitch = glm::radians(10.0f);
				camera.targetPitch += (defaultPitch - camera.targetPitch) * alignSpeed * aElapsedTime;
			}
		}

		// ==============================================================
		// 4. camera lerp 平滑执行
		// ==============================================================
		float smoothness = 6.0f;
		camera.cameraFov = kFixedGameplayFov;
		camera.Yaw += (camera.targetYaw - camera.Yaw) * smoothness * aElapsedTime;
		camera.Pitch += (camera.targetPitch - camera.Pitch) * smoothness * aElapsedTime;
		camera.Distance += (camera.targetDistance - camera.Distance) * smoothness * aElapsedTime;

		float rollMultiplier = 0.6f;
		if (aState.isExtremeSpeed) {
			// 极速状态下，镜头跟随单车压弯
			camera.targetCameraRoll = aState.bikeLeanAngle * rollMultiplier;
		}
		else {
			// 普通状态下，目标倾角归零（保持地平线水平）
			camera.targetCameraRoll = 0.0f;
		}

		// 【注意】：这行插值代码必须放在 if-else 外面！
		// 这样当玩家从极速降回普通速度时，镜头会带着重量感“缓缓回正”，而不是瞬间闪回 0 度。
		camera.cameraRoll += (camera.targetCameraRoll - camera.cameraRoll) * smoothness * aElapsedTime;

		// 计算相机位置
		glm::vec3 char_pos = aState.followTargetPos;
		glm::vec3 eye_offset(0.f, 1.2f, 0.f);
		glm::vec3 target_pos = char_pos + eye_offset;

		glm::vec3 offset;
		offset.x = camera.Distance * std::cos(camera.Pitch) * std::sin(camera.Yaw);
		offset.y = camera.Distance * std::sin(camera.Pitch);
		offset.z = camera.Distance * std::cos(camera.Pitch) * std::cos(camera.Yaw);

		glm::vec3 cam_pos = target_pos + offset;

		// 【致命检查点】：确保下面这段逻辑是这个代码块的绝对结尾！
		glm::vec3 forwardDir = glm::normalize(target_pos - cam_pos);
		glm::vec3 globalUp(0.f, 1.f, 0.f);

		// 绕视线旋转 Up 向量
		glm::mat4 rollTransform = glm::rotate(glm::mat4(1.0f), camera.cameraRoll, forwardDir);
		glm::vec3 dynamicUp = glm::vec3(rollTransform * glm::vec4(globalUp, 0.0f));

		// 唯一的 lookAt 调用，传入倾斜的 dynamicUp
		glm::mat4 view_matrix = glm::lookAt(cam_pos, target_pos, dynamicUp);
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

		auto const move = aElapsedTime * cfg::kCameraBaseSpeed *
			(inputSys->IsActionHeld("Fast") ? cfg::kCameraFastMult : 1.f) *
			(inputSys->IsActionHeld("Slow") ? cfg::kCameraSlowMult : 1.f);
		if (gameplay.isAlive)
		{
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
}

void update_scene_uniforms(glsl::SceneUniform& aSceneUniforms, std::uint32_t aFramebufferWidth, std::uint32_t aFramebufferHeight, const engine::UserState& aState)
{
	float const aspect = float(aFramebufferWidth) / float(aFramebufferHeight);
	float const fov = glm::radians(aState.camera.cameraFov);

	aSceneUniforms.projection = glm::perspectiveRH_ZO(fov, aspect, cfg::kCameraNear, cfg::kCameraFar);
	aSceneUniforms.projection[1][1] *= -1.f;
	aSceneUniforms.camera = glm::inverse(aState.camera.camera2world);
	aSceneUniforms.projCam = aSceneUniforms.projection * aSceneUniforms.camera;
	aSceneUniforms.cameraPos = glm::vec4(aState.camera.camera2world[3]);

	aSceneUniforms.lightPos = glm::vec4(-50.0f, 100.0f, -30.0f, 0.0f);
	aSceneUniforms.lightColor = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
	aSceneUniforms.renderMode = std::uint32_t(aState.renderMode);
	// ==============================================================
		// 3. 调用灯光系统计算阴影 (CSM 级联计算)
		// ==============================================================

		// 【核心修复】：不要把 `cfg::kCameraFar` 传给阴影系统！
		// 哪怕相机能看 1000 米远，我们的阴影也只包围相机前方 60 米的范围。
		// 这样无论 FOV 怎么变大，阴影盒子的最大体积都被死死限制住了，分辨率永远集中在车身附近！
	float shadowFarDistance = 550.0f; 

	engine::ShadowData shadow = engine::compute_csm_matrices(
		glm::vec3(aSceneUniforms.lightPos),
		aSceneUniforms.camera,
		fov, aspect,
		cfg::kCameraNear,
		shadowFarDistance //  cfg::kCameraFar
	);

	aSceneUniforms.cascadeSplits = shadow.cascadeSplits;
	for (int i = 0; i < 4; ++i) aSceneUniforms.lightVP[i] = shadow.lightVP[i];
}
