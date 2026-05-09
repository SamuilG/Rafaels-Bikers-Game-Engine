#pragma once

#include <volk/volk.h>
#if !defined(GLFW_INCLUDE_NONE)
#	define GLFW_INCLUDE_NONE
#endif
#include <GLFW/glfw3.h>
#include <array>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include "../../Rhi/angle.hpp"
#include "../UserState/UserState.hpp"

namespace lut = labut2;
namespace glsl {
	struct SceneUniform; // 告诉编译器这个结构体存在，具体定义稍后再看
}
namespace cfg
{
	constexpr float kCameraNear  = 0.1f;
	constexpr float kCameraFar   = 1000.f;
	constexpr auto kCameraFov    = labut2::literals::operator""_degf(60.0);
	
	constexpr float kCameraBaseSpeed = 2.0f;
	constexpr float kCameraFastMult = 5.f;
	constexpr float kCameraSlowMult = 0.05f;
	constexpr float kCameraMouseSensitivity = 0.005f;
}



namespace engine {
	struct UserState;
}


namespace engine { class InputSystem; }

namespace engine {
	enum class StereoEyeIndex : std::uint32_t {
		Left = 0,
		Right = 1
	};

	struct StereoEyeView
	{
		StereoEyeIndex eye = StereoEyeIndex::Left;
		glm::mat4 worldFromEye = glm::identity<glm::mat4>();
		glm::mat4 view = glm::identity<glm::mat4>();
		glm::mat4 projection = glm::identity<glm::mat4>();
		glm::mat4 viewProjection = glm::identity<glm::mat4>();
		glm::vec3 worldPosition = glm::vec3(0.0f);
		float nearPlane = cfg::kCameraNear;
		float farPlane = cfg::kCameraFar;
		float verticalFovRadians = glm::radians(85.0f);
	};

	struct StereoCameraFrame
	{
		bool enabled = false;
		float ipdMeters = 0.064f;
		float aspectRatio = 1.0f;
		StereoEyeView left{};
		StereoEyeView right{};

		std::array<StereoEyeView, 2> AsArray() const {
			return { left, right };
		}
	};

	struct StereoRenderTargetDesc
	{
		std::uint32_t width = 0;
		std::uint32_t height = 0;
		VkFormat colorFormat = VK_FORMAT_UNDEFINED;
		VkFormat depthFormat = VK_FORMAT_UNDEFINED;
	};

	StereoCameraFrame build_stereo_camera_frame(
		const engine::UserState& state,
		std::uint32_t framebufferWidth,
		std::uint32_t framebufferHeight,
		float ipdMeters = 0.064f);
}

void update_user_state(engine::UserState& aState, float aElapsedTime, engine::InputSystem* inputSys);

void update_scene_uniforms(glsl::SceneUniform& aSceneUniforms, std::uint32_t aFramebufferWidth, std::uint32_t aFramebufferHeight, const engine::UserState& aState);



