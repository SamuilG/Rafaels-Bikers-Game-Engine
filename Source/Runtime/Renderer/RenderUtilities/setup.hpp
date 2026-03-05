#pragma once

#include <volk/volk.h>
#include "../../Rhi/vulkan_window.hpp"
#include "../../Rhi/vkobject.hpp"
#include "../../Rhi/vkimage.hpp"
#include "camera.hpp"

namespace lut = labut2;
constexpr std::uint32_t kShadowMapResolution = 2048; // 2048 for high quality; also tested with lower values
constexpr std::uint32_t kCascadeCount = 4;




struct ShadowMapResources {
	lut::ImageWithView mainArray;           // 用于 Shader 采样的 2D_ARRAY 视图
	std::vector<VkImageView> cascadeViews;  // 用于渲染的 K 个单层视图
};


namespace glsl {
	struct SceneUniform {
		glm::mat4 camera;
		glm::mat4 projection;
		glm::mat4 projCam;
		glm::vec4 cameraPos;
		glm::vec4 lightPos;
		glm::vec4 lightColor;
		uint32_t renderMode;
		float _pad0[3];
		glm::mat4 lightVP[4];      // CSM 数组
		glm::vec4 cascadeSplits;   // CSM 分割点
	};
}

namespace cfg
{
	// Compiled shader code for the graphics pipeline
	// See sources in a12/shaders/*. 
#	define SHADERDIR_ "Assets/Shaders/spirv/"
	constexpr char const* kVertShaderPath = SHADERDIR_ "default.vert.spv";
	constexpr char const* kFragShaderPath = SHADERDIR_ "default.frag.spv";
	
	constexpr VkFormat kDepthFormat = VK_FORMAT_D32_SFLOAT;

	constexpr char const* kAlphaVertShaderPath = SHADERDIR_ "default.vert.spv";
	constexpr char const* kAlphaFragShaderPath = SHADERDIR_ "alpha.frag.spv";
	
	constexpr char const* kDebugVertShaderPath = SHADERDIR_ "debug.vert.spv";
	constexpr char const* kDebugMipFragShaderPath = SHADERDIR_ "debug_mip.frag.spv";
	constexpr char const* kDebugDepthFragShaderPath = SHADERDIR_ "debug_depth.frag.spv";
	constexpr char const* kDebugDerivFragShaderPath = SHADERDIR_ "debug_deriv.frag.spv";
	constexpr char const* kFullscreenVertShaderPath = SHADERDIR_ "fullscreen.vert.spv";
	constexpr char const* kFullscreenFragShaderPath = SHADERDIR_ "fullscreen.frag.spv";

	constexpr char const* kOverdrawFragShaderPath = SHADERDIR_ "overdraw.frag.spv";
	constexpr char const* kPassthroughFragShaderPath = SHADERDIR_ "passthrough.frag.spv";

	// p2_1.5 shadow mapping
	constexpr char const* kShadowVertShaderPath = SHADERDIR_ "shadowmap.vert.spv";
	constexpr char const* kShadowFragShaderPath = SHADERDIR_ "shadowmap.frag.spv";
	constexpr VkFormat kShadowMapFormat = VK_FORMAT_D32_SFLOAT;
	
	//Particle shaders
	constexpr char const* kParticleVertShaderPath = SHADERDIR_"particles.vert.spv";
	constexpr char const* kParticleFragShaderPath = SHADERDIR_"particles.frag.spv";

#	undef SHADERDIR_

	//particle textures
	constexpr char const* ParticleTextures[] = {
	"assets/models/fire.png",
	"assets/models/aa.png"
	};

}

struct ImageAndView
{
	VkImage image;
	VkImageView view;
};


lut::DescriptorSetLayout create_scene_descriptor_layout( lut::VulkanWindow const& );
lut::DescriptorSetLayout create_object_descriptor_layout( lut::VulkanWindow const& );
lut::DescriptorSetLayout create_post_proc_descriptor_layout( lut::VulkanWindow const& );

lut::ImageWithView create_depth_buffer( lut::VulkanWindow const&, lut::Allocator const& );
lut::ImageWithView create_offscreen_buffer( lut::VulkanWindow const&, lut::Allocator const& );
lut::ImageWithView create_vis_image( lut::VulkanWindow const&, lut::Allocator const& );

// p2_1.5 shadow mapping
lut::ImageWithView create_shadow_map( lut::VulkanWindow const&, lut::Allocator const& );
lut::Sampler create_shadow_sampler( lut::VulkanWindow const& );

lut::PipelineLayout create_triangle_pipeline_layout( lut::VulkanContext const&, VkDescriptorSetLayout, VkDescriptorSetLayout );
lut::PipelineLayout create_post_proc_pipeline_layout( lut::VulkanContext const&, VkDescriptorSetLayout );

lut::Pipeline create_triangle_pipeline( lut::VulkanWindow const&, VkPipelineLayout, VkFormat = VK_FORMAT_B8G8R8A8_SRGB );
lut::Pipeline create_debug_pipeline( lut::VulkanWindow const&, VkPipelineLayout, char const* aVertPath, char const* aFragPath, VkFormat = VK_FORMAT_B8G8R8A8_SRGB );
lut::Pipeline create_alpha_pipeline( lut::VulkanWindow const&, VkPipelineLayout, VkFormat = VK_FORMAT_B8G8R8A8_SRGB );
lut::Pipeline create_post_proc_pipeline( lut::VulkanWindow const&, VkPipelineLayout, VkDescriptorSetLayout );

lut::Pipeline create_overdraw_pipeline( lut::VulkanWindow const&, VkPipelineLayout, VkFormat = VK_FORMAT_R8G8B8A8_UNORM );
lut::Pipeline create_overshading_pipeline( lut::VulkanWindow const&, VkPipelineLayout, VkFormat = VK_FORMAT_R8G8B8A8_UNORM );
lut::Pipeline create_vis_resolve_pipeline( lut::VulkanWindow const&, VkPipelineLayout, VkDescriptorSetLayout );

// p2_1.5 shadow mapping
lut::Pipeline create_shadow_pipeline( lut::VulkanWindow const&, VkPipelineLayout );

lut::Pipeline create_particle_pipeline(lut::VulkanWindow const&, VkPipelineLayout, VkFormat);

lut::Sampler create_debug_sampler( lut::VulkanWindow const& );
lut::Sampler create_post_proc_sampler( lut::VulkanWindow const& );
