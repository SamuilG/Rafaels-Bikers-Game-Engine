#pragma once

#include <volk/volk.h>
#if !defined(GLFW_INCLUDE_NONE)
#	define GLFW_INCLUDE_NONE
#endif
#include <GLFW/glfw3.h>
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
	constexpr float kCameraFar   = 500.f;
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

void update_user_state(engine::UserState& aState, float aElapsedTime, engine::InputSystem* inputSys);

void update_scene_uniforms(glsl::SceneUniform& aSceneUniforms, std::uint32_t aFramebufferWidth, std::uint32_t aFramebufferHeight, const engine::UserState& aState);



