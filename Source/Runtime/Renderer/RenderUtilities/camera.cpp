#include "camera.hpp"
#include "setup.hpp"
#include "light.hpp"
#include "../../Input/InputSystem.hpp"
#include "../../UserState/EditorState.hpp"
#include <algorithm>

void update_camera(engine::CameraController& controller, const engine::PlayerState& player,
                   const engine::EditorState& editor, float dt, engine::InputSystem* inputSys)
{
    engine::CameraInput input;
    if (inputSys) {
        const bool acceptsInput = !editor.showEngineUi || editor.isSceneViewportHovered;
        if (!acceptsInput && inputSys->IsMouseCaptured()) inputSys->SetMouseCaptured(false);
        if (acceptsInput && inputSys->IsActionPressed("CaptureMouse"))
            inputSys->SetMouseCaptured(!inputSys->IsMouseCaptured());
        if (acceptsInput) {
            const glm::vec2 mouse = inputSys->IsMouseCaptured() ? inputSys->GetMouseDelta() : glm::vec2(0.0f);
            input.lookDelta = mouse * cfg::kCameraMouseSensitivity + inputSys->GetGamepadRightStick() * (2.5f * dt);
            input.distanceDelta = -inputSys->GetScrollY() * 9.0f;
            input.fovDelta = ((inputSys->IsActionHeld("ZoomOut") ? 1.0f : 0.0f) -
                              (inputSys->IsActionHeld("ZoomIn") ? 1.0f : 0.0f)) * 70.0f * dt;
            const float speed = cfg::kCameraBaseSpeed *
                (inputSys->IsActionHeld("Fast") ? cfg::kCameraFastMult : 1.0f) *
                (inputSys->IsActionHeld("Slow") ? cfg::kCameraSlowMult : 1.0f);
            input.localMove = glm::vec3(
                (inputSys->IsActionHeld("StrafeRight") ? 1.0f : 0.0f) - (inputSys->IsActionHeld("StrafeLeft") ? 1.0f : 0.0f),
                (inputSys->IsActionHeld("Upward") ? 1.0f : 0.0f) - (inputSys->IsActionHeld("Downward") ? 1.0f : 0.0f),
                (inputSys->IsActionHeld("MoveBackward") ? 1.0f : 0.0f) - (inputSys->IsActionHeld("MoveForward") ? 1.0f : 0.0f)) * speed;
        }
    }
    input.allowFreeMovement = player.isAlive || editor.showEngineUi;
    controller.Update(dt, input, player);
}

void update_scene_uniforms(glsl::SceneUniform& aSceneUniforms, std::uint32_t aFramebufferWidth, std::uint32_t aFramebufferHeight, const engine::CameraState& camera, int renderMode)
{
	float const aspect = float(aFramebufferWidth) / float(aFramebufferHeight);
	float const fov = glm::radians(camera.cameraFov);

	aSceneUniforms.projection = glm::perspectiveRH_ZO(fov, aspect, cfg::kCameraNear, cfg::kCameraFar);
	aSceneUniforms.projection[1][1] *= -1.f;
	aSceneUniforms.camera = glm::inverse(camera.camera2world);
	aSceneUniforms.projCam = aSceneUniforms.projection * aSceneUniforms.camera;
	aSceneUniforms.cameraPos = glm::vec4(camera.camera2world[3]);

	aSceneUniforms.lightPos = glm::vec4(50.0f, 100.0f, -30.0f, 0.0f);
	aSceneUniforms.lightColor = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
	aSceneUniforms.renderMode = std::uint32_t(renderMode);
	// ==============================================================
		// 3. 调用灯光系统计算阴影 (CSM 级联计算)
		// ==============================================================

		// 【核心修复】：不要把 `cfg::kCameraFar` 传给阴影系统！
		// 哪怕相机能看 1000 米远，我们的阴影也只包围相机前方 60 米的范围。
		// 这样无论 FOV 怎么变大，阴影盒子的最大体积都被死死限制住了，分辨率永远集中在车身附近！
	float shadowFarDistance = 850.0f; 

	engine::ShadowData shadow = engine::compute_csm_matrices(
		glm::vec3(aSceneUniforms.lightPos),
		aSceneUniforms.camera,
		fov, aspect,
		cfg::kCameraNear,
		shadowFarDistance //  cfg::kCameraFar
	);

	aSceneUniforms.cascadeSplits = shadow.cascadeSplits;
	for (int i = 0; i < 4; ++i) aSceneUniforms.lightVP[i] = shadow.lightVP[i];
	aSceneUniforms.portalClipPlane = glm::vec4(0.0f);
}
