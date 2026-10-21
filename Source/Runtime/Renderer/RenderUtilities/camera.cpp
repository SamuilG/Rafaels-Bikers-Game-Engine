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
        // Once GLFW disables the cursor, ImGui can report the viewport as not
        // hovered for a frame because the cursor is no longer inside the
        // window. A captured editor camera must keep consuming input until the
        // user presses the capture button again; otherwise a short drag loses
        // control and requires a second middle-click.
        const bool mouseCaptured = inputSys->IsMouseCaptured();
        const bool acceptsInput = !editor.showEngineUi || editor.isSceneViewportHovered || mouseCaptured;
        if (acceptsInput) {
            const glm::vec2 mouse = inputSys->IsMouseCaptured() ? inputSys->GetMouseDelta() : glm::vec2(0.0f);
            input.lookDelta = mouse * cfg::kCameraMouseSensitivity + inputSys->GetGamepadRightStick() * (2.5f * dt);
            input.distanceDelta = -inputSys->GetScrollY() * 9.0f;
            input.fovDelta = ((inputSys->IsActionHeld("ZoomOut") ? 1.0f : 0.0f) -
                              (inputSys->IsActionHeld("ZoomIn") ? 1.0f : 0.0f)) * 70.0f * dt;
            const float speed = cfg::kCameraBaseSpeed *
                (inputSys->IsActionHeld("Fast") ? cfg::kCameraFastMult : 1.0f) *
                (inputSys->IsActionHeld("Slow") ? cfg::kCameraSlowMult : 1.0f);
            // Uncaptured Scene keys belong to the editor tools (Q/W/E/R/F).
            // Fly navigation starts only after explicitly capturing the mouse.
            input.localMove = editor.showEngineUi && !mouseCaptured ? glm::vec3(0.0f) : glm::vec3(
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

	aSceneUniforms.projection = glm::perspectiveRH_ZO(fov, aspect, camera.nearPlane, camera.farPlane);
	aSceneUniforms.projection[1][1] *= -1.f;
	aSceneUniforms.camera = glm::inverse(camera.camera2world);
	aSceneUniforms.projCam = aSceneUniforms.projection * aSceneUniforms.camera;
	aSceneUniforms.cameraPos = glm::vec4(camera.camera2world[3]);

	aSceneUniforms.lightPos = glm::vec4(0.0f);
	aSceneUniforms.lightColor = glm::vec4(0.0f);
	aSceneUniforms.renderMode = std::uint32_t(renderMode);
    // Lighting is populated from the selected scene light after camera setup.
	engine::ShadowData shadow{};

	aSceneUniforms.cascadeSplits = shadow.cascadeSplits;
	for (int i = 0; i < 4; ++i) aSceneUniforms.lightVP[i] = shadow.lightVP[i];
	aSceneUniforms.portalClipPlane = glm::vec4(0.0f);
	aSceneUniforms.directionalShadow.raster.z = 0.0f;
}

void update_directional_shadows(glsl::SceneUniform& uniforms, const engine::DirectionalLightState& light,
    float nearPlane, float farPlane) {
    const float fov = 2.0f * std::atan(1.0f / std::abs(uniforms.projection[1][1]));
    const float aspect = std::abs(uniforms.projection[1][1] / uniforms.projection[0][0]);
    const auto frame = engine::BuildDirectionalShadowFrame(light, uniforms.camera, fov, aspect, nearPlane, farPlane);
    uniforms.lightPos = frame.source.position;
    uniforms.lightColor = frame.source.color;
    uniforms.directionalShadow = frame.parameters;
    uniforms.cascadeSplits = frame.cascades.cascadeSplits;
    for (int i = 0; i < 3; ++i) uniforms.lightVP[i] = frame.cascades.lightVP[i];
}
