#pragma once

#include <volk/volk.h>
#include "../../Rhi/vulkan_window.hpp"
#include "../../Rhi/vkobject.hpp"
#include "../../Rhi/vkimage.hpp"
#include "camera.hpp"
#include "light.hpp"


namespace lut = labut2;
constexpr std::uint32_t kShadowMapResolution = 2048; // 2048 for high quality; also tested with lower values
constexpr std::uint32_t kCascadeCount = 4;

namespace engine {
	enum class LightType : uint32_t;

}


struct ShadowMapResources {
	lut::ImageWithView mainArray;           // 用于 Shader 采样�?2D_ARRAY 视图
	std::vector<VkImageView> cascadeViews;  // 用于渲染�?K 个单层视�?
};


namespace glsl {
	struct SceneUniform {
		glm::mat4 camera;
		glm::mat4 projection;
		glm::mat4 projCam;
		glm::vec4 cameraPos;
		engine::GpuLight lights[16];

		glm::vec4 lightPos;
		glm::vec4 lightColor;

		uint32_t lightCount;
		uint32_t renderMode;
		uint32_t _pad0;
		uint32_t _pad1;

		glm::mat4 lightVP[4];
		glm::vec4 cascadeSplits;
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
	constexpr std::uint32_t kMaxGpuBoneMatrices = 256;

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

	//

	constexpr char const* kBlurShaderPath = SHADERDIR_ "blur.frag.spv";
	constexpr char const* kComShaderPath = SHADERDIR_ "composite.frag.spv";

	//Debug Line
	constexpr char const* kDebugLineVertShaderPath = SHADERDIR_"debug_line.vert.spv";
	constexpr char const* kDebugLineFragShaderPath = SHADERDIR_"debug_line.frag.spv";


	constexpr char const* kSpeedPostFragShaderPath = SHADERDIR_"speed_postprocess.frag.spv";

	//skybox
	constexpr char const* skyboxVertShaderPath = SHADERDIR_"skybox.vert.spv";
	constexpr char const* skyboxFragShaderPath = SHADERDIR_"skybox.frag.spv";
	// Skeletal skinning
	constexpr char const* kSkinnedVertShaderPath = SHADERDIR_"skinned.vert.spv";


#	undef SHADERDIR_

	//particle textures
	constexpr char const* ParticleTextures[] = {
	"Assets/Textures/MainMenu_Bg.png",
	"Assets/Textures/fire.png",
	"Assets/Textures/aa.png",
	"Assets/Textures/fire5.png",
	"Assets/Textures/ww.png",
	"Assets/Textures/particleIcon.png",
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
lut::PipelineLayout create_blur_pipeline_layout(lut::VulkanContext const& aContext, VkDescriptorSetLayout aDescriptorLayout);
lut::PipelineLayout create_composite_pipeline_layout(lut::VulkanContext const& aContext, VkDescriptorSetLayout aDescriptorLayout);
lut::DescriptorSetLayout create_composite_descriptor_layout(lut::VulkanWindow const& aWindow);
lut::DescriptorSetLayout create_blur_descriptor_layout(lut::VulkanContext const& aContext);

lut::Pipeline create_triangle_pipeline( lut::VulkanWindow const&, VkPipelineLayout, VkFormat = VK_FORMAT_B8G8R8A8_SRGB );
lut::Pipeline create_debug_pipeline( lut::VulkanWindow const&, VkPipelineLayout, char const* aVertPath, char const* aFragPath, VkFormat = VK_FORMAT_B8G8R8A8_SRGB );
lut::Pipeline create_alpha_pipeline( lut::VulkanWindow const&, VkPipelineLayout, VkFormat = VK_FORMAT_B8G8R8A8_SRGB );
lut::Pipeline create_alpha_pipeline_1_attachment( lut::VulkanWindow const&, VkPipelineLayout, VkFormat = VK_FORMAT_B8G8R8A8_SRGB );
lut::Pipeline create_post_proc_pipeline( lut::VulkanWindow const&, VkPipelineLayout, VkDescriptorSetLayout );
lut::Pipeline create_blur_pipeline(lut::VulkanWindow const& aWindow, VkPipelineLayout aPipelineLayout);
lut::Pipeline create_composite_pipeline(lut::VulkanWindow const& aWindow, VkPipelineLayout aPipelineLayout);
lut::Pipeline create_overdraw_pipeline( lut::VulkanWindow const&, VkPipelineLayout, VkFormat = VK_FORMAT_R8G8B8A8_UNORM );
lut::Pipeline create_overshading_pipeline( lut::VulkanWindow const&, VkPipelineLayout, VkFormat = VK_FORMAT_R8G8B8A8_UNORM );
lut::Pipeline create_vis_resolve_pipeline( lut::VulkanWindow const&, VkPipelineLayout, VkDescriptorSetLayout );
lut::PipelineLayout create_speed_post_pipeline_layout(lut::VulkanContext const& aContext, VkDescriptorSetLayout aDescriptorLayout);
lut::Pipeline create_speed_post_pipeline(lut::VulkanWindow const& aWindow, VkPipelineLayout aPipelineLayout);
// p2_1.5 shadow mapping
lut::Pipeline create_shadow_pipeline( lut::VulkanWindow const&, VkPipelineLayout );

lut::Pipeline create_particle_pipeline(lut::VulkanWindow const&, VkPipelineLayout, VkFormat);

lut::Sampler create_debug_sampler( lut::VulkanWindow const& );
lut::Sampler create_post_proc_sampler( lut::VulkanWindow const& );


lut::DescriptorSetLayout create_skybox_descriptor_layout(lut::VulkanWindow const& window);
lut::PipelineLayout create_skybox_pipeline_layout(lut::VulkanWindow const& window, VkDescriptorSetLayout dsetLayout);
lut::Pipeline create_skybox_pipeline(lut::VulkanWindow const& window, VkPipelineLayout pipeLayout, VkFormat colorFormat);



// debug render
lut::Pipeline create_debug_line_pipeline(lut::VulkanWindow const& aWindow, VkPipelineLayout aPipelineLayout, VkFormat aColorFormat);

// ------- Skeletal animation / skinning -------
// Descriptor set layout for the bone-matrices SSBO (set = 2, binding = 0)
lut::DescriptorSetLayout create_bone_descriptor_layout(lut::VulkanContext const& aContext);

// Pipeline layout: [set0=scene, set1=material, set2=bones] + 128-byte push constants
lut::PipelineLayout create_skinned_pipeline_layout(lut::VulkanContext const& aContext,
                                                   VkDescriptorSetLayout aSceneLayout,
                                                   VkDescriptorSetLayout aObjectLayout,
                                                   VkDescriptorSetLayout aBoneLayout);

// Skinned opaque pipeline (uses skinned.vert.spv + default.frag.spv, 5 vertex bindings)
lut::Pipeline create_skinned_pipeline(lut::VulkanWindow const& aWindow,
                                      VkPipelineLayout aPipelineLayout,
                                      VkFormat aColorFormat = VK_FORMAT_B8G8R8A8_SRGB);

// Skinned alpha-masked pipeline
lut::Pipeline create_skinned_alpha_pipeline(lut::VulkanWindow const& aWindow,
                                            VkPipelineLayout aPipelineLayout,
                                            VkFormat aColorFormat = VK_FORMAT_B8G8R8A8_SRGB);
