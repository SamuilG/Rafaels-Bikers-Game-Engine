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



namespace lut = labut2;

void glfw_callback_key_press( GLFWwindow* aWindow, int aKey, int, int aAction, int )
{
	auto* state = static_cast<UserState*>( glfwGetWindowUserPointer( aWindow ) );
	if( !state ) return;

	bool const isAns = (GLFW_PRESS == aAction || GLFW_REPEAT == aAction);
	
	if( GLFW_KEY_ESCAPE == aKey && GLFW_PRESS == aAction )
		glfwSetWindowShouldClose( aWindow, GLFW_TRUE );

	// WSAD and E/Q
	if( GLFW_KEY_W == aKey ) state->inputMap[std::size_t(EInputState::forward)] = isAns;
	if( GLFW_KEY_S == aKey ) state->inputMap[std::size_t(EInputState::backward)] = isAns;
	if( GLFW_KEY_A == aKey ) state->inputMap[std::size_t(EInputState::strafeLeft)] = isAns;
	if( GLFW_KEY_D == aKey ) state->inputMap[std::size_t(EInputState::strafeRight)] = isAns;
	if( GLFW_KEY_E == aKey ) state->inputMap[std::size_t(EInputState::levitate)] = isAns;
	if( GLFW_KEY_Q == aKey ) state->inputMap[std::size_t(EInputState::sink)] = isAns;

	// Fast
	if( GLFW_KEY_LEFT_SHIFT == aKey || GLFW_KEY_RIGHT_SHIFT == aKey )
		state->inputMap[std::size_t(EInputState::fast)] = isAns;
	if( GLFW_KEY_LEFT_CONTROL == aKey || GLFW_KEY_RIGHT_CONTROL == aKey )
		state->inputMap[std::size_t(EInputState::slow)] = isAns;

	// Task 1.4
	// Debug Modes
	// keys 1-4: switch the pipeline used for drawing
	// 1: default (textured)
	// 2: mipmap visualization (blocky/colored levels)
	// 3: linearized depth
	// 4: partial derivatives of depth (edges)
	if( GLFW_PRESS == aAction )
	{
		if( GLFW_KEY_1 == aKey ) state->renderMode = 0; // Default
		if( GLFW_KEY_2 == aKey ) state->renderMode = 1; // Mipmap
		if( GLFW_KEY_3 == aKey ) state->renderMode = 2; // Depth
		if( GLFW_KEY_4 == aKey ) state->renderMode = 3; // Derivatives
		if( GLFW_KEY_5 == aKey ) state->mosaicEnabled = !state->mosaicEnabled; // Mosaic Toggle
		if( GLFW_KEY_6 == aKey ) state->renderMode = 4; // Overdraw
		if( GLFW_KEY_7 == aKey ) state->renderMode = 5; // Overshading
		if( GLFW_KEY_8 == aKey ) state->renderMode = 6; // Shadow Debug (Task p2_1.5)
		
		if( GLFW_KEY_P == aKey )
		{												// Print camera position
			auto const pos = state->camera2world[3];
			std::printf("Camera Pos: %.4f, %.4f, %.4f\n", pos.x, pos.y, pos.z);
		}

		if (GLFW_KEY_R == aKey)//particle system toggle 粒子系统开关
		{
			state->particlesEnabled = !state->particlesEnabled;
			std::printf("Particles: %s\n", state->particlesEnabled ? "ON" : "OFF");
		}
	}
}

void glfw_callback_button( GLFWwindow* aWindow, int aButton, int aAction, int )
{
	auto* state = static_cast<UserState*>( glfwGetWindowUserPointer( aWindow ) );
	if( !state ) return;

	if( GLFW_MOUSE_BUTTON_RIGHT == aButton && GLFW_PRESS == aAction )
	{
		state->previousMouseState = !state->previousMouseState;
		if( state->previousMouseState )
			glfwSetInputMode( aWindow, GLFW_CURSOR, GLFW_CURSOR_DISABLED );
		else
			glfwSetInputMode( aWindow, GLFW_CURSOR, GLFW_CURSOR_NORMAL );
	}
}

void glfw_callback_motion( GLFWwindow* aWindow, double aPosX, double aPosY )
{
	auto* state = static_cast<UserState*>( glfwGetWindowUserPointer( aWindow ) );
	if( !state ) return;

	state->mouseX = float(aPosX);
	state->mouseY = float(aPosY);
}

void update_user_state( UserState& aState, float aElapsedTime )
{
	auto& cam = aState.camera2world;

	if( aState.previousMouseState )
	{
		if( aState.wasMousing )
		{
			auto const sens = cfg::kCameraMouseSensitivity;
			auto const dx = (aState.mouseX - aState.mouseLastX) * sens;
			auto const dy = (aState.mouseY - aState.mouseLastY) * sens;

			// Rotation
			auto const pos = cam[3];
			cam[3] = glm::vec4( 0.f, 0.f, 0.f, 1.f );
			cam = glm::rotate( -dx, glm::vec3( 0.f, 1.f, 0.f ) ) * cam;
			cam[3] = pos;

			cam = glm::rotate( cam, -dy, glm::vec3( 1.f, 0.f, 0.f ) ); 
		}

		aState.mouseLastX = aState.mouseX;
		aState.mouseLastY = aState.mouseY;
		aState.wasMousing = true;
	}
	else
	{
		aState.wasMousing = false;
	}

	auto const move = aElapsedTime * cfg::kCameraBaseSpeed *
		(aState.inputMap[std::size_t(EInputState::fast)] ? cfg::kCameraFastMult : 1.f) *
		(aState.inputMap[std::size_t(EInputState::slow)] ? cfg::kCameraSlowMult : 1.f);

	if( aState.inputMap[std::size_t(EInputState::forward)] )
		cam = cam * glm::translate( glm::vec3( 0.f, 0.f, -move ) );
	if( aState.inputMap[std::size_t(EInputState::backward)] )
		cam = cam * glm::translate( glm::vec3( 0.f, 0.f, +move ) );
	
	if( aState.inputMap[std::size_t(EInputState::strafeLeft)] )
		cam = cam * glm::translate( glm::vec3( -move, 0.f, 0.f ) );
	if( aState.inputMap[std::size_t(EInputState::strafeRight)] )
		cam = cam * glm::translate( glm::vec3( +move, 0.f, 0.f ) );

	if( aState.inputMap[std::size_t(EInputState::levitate)] )
		cam = cam * glm::translate( glm::vec3( 0.f, +move, 0.f ) );
	if( aState.inputMap[std::size_t(EInputState::sink)] )
		cam = cam * glm::translate( glm::vec3( 0.f, -move, 0.f ) );
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
