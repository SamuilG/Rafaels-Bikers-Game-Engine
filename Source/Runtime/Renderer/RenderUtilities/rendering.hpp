#pragma once

#include <volk/volk.h>
#include <vector>
#include <memory>

#include "setup.hpp"
#include "camera.hpp"
//#include "engine_model.hpp"
#include "../../Rhi/vkobject.hpp"
#include "../../Rhi/vulkan_window.hpp"
#include "../../Rhi/vkbuffer.hpp" 
#include "../../Scene/SceneManager.hpp"
#include "../../Particle/ParticleSystem.hpp"
#include "../../Debug/DebugRenderer.hpp"

namespace lut = labut2;



// 必须与 .cpp 中的签名完全匹配
void record_commands(
	VkCommandBuffer aCmdBuff,
	VkPipeline aGraphicsPipe,
	VkPipeline aAlphaPipe,
	ImageAndView const& aSwapchainAttach,
	ImageAndView const& aDepthAttach,
	VkExtent2D const& aImageExtent,
	VkBuffer aSceneUBO,
	glsl::SceneUniform const& aSceneUniform,
	VkPipelineLayout aGraphicsLayout,
	VkDescriptorSet aSceneDescriptors,
	std::vector<lut::Buffer> const& aMeshPositions,
	std::vector<lut::Buffer> const& aMeshTexCoords,
	std::vector<lut::Buffer> const& aMeshNormals,
	std::vector<lut::Buffer> const& aMeshIndices,
	std::vector<EngineMesh> const& aMeshInfos,
	std::vector<EngineMaterial> const& aMaterials,
	std::vector<VkDescriptorSet> const& aMaterialDescriptors,
	std::vector<RenderBatch> const& aBatches,
	// --- Bloom / blur / composite 新增参数 ---
	VkPipeline aBlurPipe,
	VkPipelineLayout aBlurLayout,
	VkPipeline aCompositePipe,
	VkPipelineLayout aCompositeLayout,
	VkDescriptorSet aBlurHorizDS,
	VkDescriptorSet aBlurVertDS,
	VkDescriptorSet aCompositeDS,
	ImageAndView const& aOffscreenColor,
	ImageAndView const& aBrightColor,
	ImageAndView const& aBlurTemp,
	ImageAndView const& aFinalBloom,
	ImageAndView const& aCompositeOutput, // 【修改】：原来是 aFinalSceneColor，现在变成中转输出图
	VkClearColorValue aClearColor,
	float aBloomStrength,

	// ==============================================================
	// 极速后期特效参数
	// ==============================================================
	VkPipeline aSpeedPipe,
	VkPipelineLayout aSpeedLayout,
	VkDescriptorSet aSpeedDesc,
	float aSpeedFactor,
	ImageAndView const& aFinalSceneColor, //这才是真正的最终画面（交给 ImGui）
	// ==============================================================

	// --- 原有后处理与阴影/粒子参数保留 ---
	VkPipeline aPostProcPipe,
	VkDescriptorSet aPostProcDescriptors,
	VkPipelineLayout aPostProcLayout,
	VkPipeline aShadowPipe,
	ImageAndView const& aShadowMap,
	std::vector<VkImageView> const& aShadowCascadeViews,
	bool particlesEnabled,
	VkPipeline particlePipe,
	const std::vector<std::unique_ptr<ParticleSystem>>& allParticles,
	VkPipeline aDebugLinePipe,
	engine::DebugRenderer& aDebugRenderer
);

void submit_commands( 
	lut::VulkanContext const& aContext, 
	VkCommandBuffer aCmdBuff, 
	VkFence aFence, 
	VkSemaphore aWaitSemaphore, 
	VkSemaphore aSignalSemaphore 
);

void present_results( 
	VkQueue aPresentQueue, 
	VkSwapchainKHR aSwapchain, 
	std::uint32_t aImageIndex, 
	VkSemaphore aRenderFinished, 
	bool& aNeedToRecreateSwapchain 
);
