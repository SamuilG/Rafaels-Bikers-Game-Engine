#pragma once

#include <volk/volk.h>
#if !defined(GLFW_INCLUDE_NONE)
#	define GLFW_INCLUDE_NONE
#endif
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include "../labut2/angle.hpp"

namespace lut = labut2;

namespace cfg
{
	constexpr float kCameraNear  = 0.1f;
	constexpr float kCameraFar   = 100.f;
	constexpr auto kCameraFov    = labut2::literals::operator""_degf(60.0);
	
	constexpr float kCameraBaseSpeed = 1.7f;
	constexpr float kCameraFastMult = 5.f;
	constexpr float kCameraSlowMult = 0.05f;
	constexpr float kCameraMouseSensitivity = 0.01f;
}

namespace glsl
{
	struct SceneUniform
	{
// existing members
		glm::mat4 camera;
		glm::mat4 projection;
		glm::mat4 projCam;
		glm::vec4 cameraPos;
		glm::vec4 lightPos;
		glm::vec4 lightColor;
		std::uint32_t renderMode;
		std::uint32_t _padding[3];
		glm::mat4 lightVP; // p2_1.5
	};
}

enum class EInputState
{
	forward,
	backward,
	strafeLeft,
	strafeRight,
	levitate,
	sink,
	fast,
	slow,
	max
};

struct UserState
{
	bool inputMap[std::size_t(EInputState::max)] = {};

	float mouseX = 0.f;
	float mouseY = 0.f;
	
	float mouseLastX = 0.f;
	float mouseLastY = 0.f;

	bool previousMouseState = false; // true = right button held
	bool wasMousing = false; // true = camera active

	glm::mat4 camera2world = glm::identity<glm::mat4>();

	int renderMode = 0; // 0=Default, 1=Mip, 2=Depth, 3=Deriv
	
	bool mosaicEnabled = false; // key 5 toggle
};

// GLFW callbacks
void glfw_callback_key_press( GLFWwindow* aWindow, int aKey, int, int aAction, int );
void glfw_callback_button( GLFWwindow* aWindow, int aButton, int aAction, int );
void glfw_callback_motion( GLFWwindow* aWindow, double aPosX, double aPosY );

void update_user_state( UserState& aState, float aElapsedTime );

void update_scene_uniforms(
	glsl::SceneUniform& aSceneUniforms,
	std::uint32_t aFramebufferWidth,
	std::uint32_t aFramebufferHeight,
	UserState const& aState
);
