#pragma once

#include <volk/volk.h>
#include <vector>
#include <memory>
#include <unordered_map>

#include "setup.hpp"
#include "camera.hpp"
#include "../../Rhi/vkobject.hpp"
#include "../../Rhi/vulkan_window.hpp"
#include "../../Rhi/vkbuffer.hpp"
#include "../../Scene/SceneManager.hpp"
#include "../../Particle/ParticleSystem.hpp"
#include "../../Debug/DebugRenderer.hpp"

namespace lut = labut2;


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
	// --- Bloom / blur / composite ---
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
	ImageAndView const& aCompositeOutput, // modified from aFinalSceneColor; used for bloom transfer
	VkClearColorValue aClearColor,
	float aBloomStrength,

	VkPipeline aSsrPipe,
	VkPipelineLayout aSsrLayout,
	VkDescriptorSet aSsrDS,
	ImageAndView const& aSsrOutput,
	// ==============================================================
	// 极速后处理效果
	// ==============================================================
	VkPipeline aSpeedPipe,
	VkPipelineLayout aSpeedLayout,
	VkDescriptorSet aSpeedDesc,
	float aSpeedFactor,
	ImageAndView const& aFinalSceneColor, // 最终场景渲染缓冲区（输出到 ImGui）
	// ==============================================================

	// --- 原有后处理/阴影/粒子参数 ---
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
	engine::DebugRenderer& aDebugRenderer,
	// --- Skeletal skinning (optional; pass VK_NULL_HANDLE to skip) ---
	VkPipeline aSkinnedPipe                                          = VK_NULL_HANDLE,
	VkPipeline aSkinnedAlphaPipe                                     = VK_NULL_HANDLE,
	VkPipelineLayout aSkinnedPipeLayout                              = VK_NULL_HANDLE,
	VkDescriptorSet  aBoneDescriptorSet                              = VK_NULL_HANDLE,
	const std::unordered_map<uint32_t, lut::Buffer>* aMeshJoints    = nullptr,
	const std::unordered_map<uint32_t, lut::Buffer>* aMeshWeights   = nullptr,
	const std::vector<RenderBatch>*                  aSkinnedBatches = nullptr,

	// 在 record_commands 的最后追加：
	VkPipeline skyboxPipe = VK_NULL_HANDLE,
	VkPipelineLayout skyboxPipeLayout = VK_NULL_HANDLE,
	VkDescriptorSet skyboxDescSet = VK_NULL_HANDLE,
	VkBuffer skyboxVBO = VK_NULL_HANDLE
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
