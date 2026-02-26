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
	// --- 1. 基础相机矩阵计算 ---
	float const aspect = float(aFramebufferWidth) / float(aFramebufferHeight);
	aSceneUniforms.projection = glm::perspectiveRH_ZO(
		lut::Radians(cfg::kCameraFov).value(),
		aspect,
		cfg::kCameraNear,
		cfg::kCameraFar
	);
	aSceneUniforms.projection[1][1] *= -1.f; // 适配 Vulkan Y 轴

	aSceneUniforms.camera = glm::inverse(aState.camera2world);
	aSceneUniforms.projCam = aSceneUniforms.projection * aSceneUniforms.camera;
	aSceneUniforms.cameraPos = glm::vec4(aState.camera2world[3][0], aState.camera2world[3][1], aState.camera2world[3][2], 1.0f);

	// 设置灯光方向 (从 lightPos 指向场景中心)
	aSceneUniforms.lightPos = glm::vec4(-50.0f, 100.0f, -30.0f, 0.0f);
	aSceneUniforms.lightColor = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
	aSceneUniforms.renderMode = std::uint32_t(aState.renderMode);
	
	// --- 2. CSM 级联分割计算 ---
	float const nearP = cfg::kCameraNear;
	float const farP = cfg::kCameraFar;
	float cascadeSplits[kCascadeCount];

	// lambda 是控制级联分割在对数空间和线性空间之间的权重。0.75f 是一个常用的经验值，可以根据需要调整。
	// 当 lambda 趋近于 1 时，分割更接近对数分割，近处细节更多；当 lambda 趋近于 0 时，分割更接近线性分割，远处细节更多。
	


	float lambda = 0.75f; 
	// 下面的循环计算每个级联的分割距离，并存储在 cascadeSplits 数组中。每个分割距离都是 nearP 和 farP 之间的一点，具体位置由 lambda 控制。

	for (uint32_t i = 0; i < kCascadeCount; i++) {
		float p = (i + 1) / static_cast<float>(kCascadeCount);
		float logSplit = nearP * std::pow(farP / nearP, p);
		float linSplit = nearP + (farP - nearP) * p;
		cascadeSplits[i] = lambda * logSplit + (1.0f - lambda) * linSplit;
	}
	aSceneUniforms.cascadeSplits = glm::vec4(cascadeSplits[0], cascadeSplits[1], cascadeSplits[2], cascadeSplits[3]);

	// --- 3. 为每个级联计算 LightVP ---

	glm::vec3 lightPosWS = glm::vec3(aSceneUniforms.lightPos);
	
	//glm::vec3 lightDir = glm::normalize(glm::vec3(-0.3f, 1.0f, -0.3f));
	glm::vec3 lightDir = glm::normalize(lightPosWS);



	float lastSplit = nearP;
	for (uint32_t i = 0; i < kCascadeCount; i++) {
		float currentSplit = cascadeSplits[i];

		// 1. 获取当前分段视锥体的世界空间 8 个顶点
		glm::mat4 segProj = glm::perspectiveRH_ZO(lut::Radians(cfg::kCameraFov).value(), aspect, lastSplit, currentSplit);
		glm::mat4 invVP = glm::inverse(segProj * aSceneUniforms.camera);



		glm::vec4 frustumCorners[8] = {
			{-1, -1, 0, 1}, {1, -1, 0, 1}, {1, 1, 0, 1}, {-1, 1, 0, 1},
			{-1, -1, 1, 1}, {1, -1, 1, 1}, {1, 1, 1, 1}, {-1, 1, 1, 1}
		};

		glm::vec3 center(0.0f);
		for (int j = 0; j < 8; j++) {
			frustumCorners[j] = invVP * frustumCorners[j];
			frustumCorners[j] /= frustumCorners[j].w;
			center += glm::vec3(frustumCorners[j]);
		}
		center /= 8.0f;

		// 2. 计算视锥体分段的外接球半径，用于构建稳定的正交矩阵
		float radius = 0.0f;
		for (int j = 0; j < 8; j++) {
			float dist = glm::length(glm::vec3(frustumCorners[j]) - center);
			radius = glm::max(radius, dist);
		}
		radius = std::ceil(radius * 16.0f) / 16.0f; // 稳定化处理

		// 3. 构建灯光观察矩阵
		// 将灯光位置沿反方向拉得极远（例如 500.f），确保涵盖视锥体前后的遮挡物
		glm::mat4 lightView = glm::lookAt(center + lightDir * radius * 2.0f, center, glm::vec3(0, 1, 0));


		float zMargin = 1000.0f;
		glm::mat4 lightOrtho = glm::orthoRH_ZO(-radius, radius, -radius, radius, -zMargin, zMargin);

		// 适配 Vulkan Y 轴
		lightOrtho[1][1] *= -1.f;

		aSceneUniforms.lightVP[i] = lightOrtho * lightView;
		lastSplit = currentSplit;
	}
}