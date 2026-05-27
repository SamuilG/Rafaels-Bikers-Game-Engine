#pragma once

#include <volk/volk.h>
#include <cstdint>
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
	ImageAndView const& aNormalImage,   // <--- 【新增】法线缓冲
	ImageAndView const& aSsrOutput,     // <--- 【新增】SSR 结果输出缓冲
	VkPipeline aSsrPipe,                // <--- 【新增】
	VkPipelineLayout aSsrLayout,        // <--- 【新增】
	VkDescriptorSet aSsrDS,             // <--- 【新增】
	// ==============================================================
	// 【新增】：SSAO (屏幕空间环境光遮蔽) 相关参数
	// ==============================================================
	ImageAndView const& aSsaoRawOutput, // SSAO 原始噪点图输出目标
	VkPipeline aSsaoPipe,               // SSAO 渲染管线
	VkPipelineLayout aSsaoLayout,       // SSAO 管线布局 (用于传 PushConstants)
	VkDescriptorSet aSsaoDS,            // SSAO 描述符集 (绑定了深度、法线、噪声贴图和采样核)
	bool aSsaoEnabled,                  // SSAO 开关
	// ==============================================================
	bool aSsrEnabled,
	ImageAndView const& aBlurTemp,
	ImageAndView const& aFinalBloom,
	ImageAndView const& aCompositeOutput, // modified from aFinalSceneColor; used for bloom transfer
	VkClearColorValue aClearColor,
	float aBloomStrength,

	// ==============================================================
	// 极速后处理效果
	// ==============================================================
	VkPipeline aSpeedPipe,
	VkPipelineLayout aSpeedLayout,
	VkDescriptorSet aSpeedDesc,
	float aSpeedFactor,
	bool isAlive,       // <--- 直接把 userState 里的变量喂给渲染器！
	float deathFactor,
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
	VkPipeline aSkinnedShadowPipe                                    = VK_NULL_HANDLE,

	// 在 record_commands 的最后追加：
	VkPipeline skyboxPipe = VK_NULL_HANDLE,
	VkPipelineLayout skyboxPipeLayout = VK_NULL_HANDLE,
	VkDescriptorSet skyboxDescSet = VK_NULL_HANDLE,
	VkBuffer skyboxVBO = VK_NULL_HANDLE,

	bool aPortalEnabled = false,
	glsl::SceneUniform const* aPortalSceneUniform = nullptr,
	glsl::SceneUniform const* aPortalRecursiveSceneUniforms = nullptr,
	uint32_t aPortalRecursiveSceneUniformCount = 0,
	const std::vector<RenderBatch>* aPortalBatches = nullptr,
	ImageAndView const* aPortalColor = nullptr,
	ImageAndView const* aPortalBright = nullptr,
	ImageAndView const* aPortalNormal = nullptr,
	ImageAndView const* aPortalDepth = nullptr,
	ImageAndView const* aPortalRecursiveColor = nullptr,
	ImageAndView const* aPortalRecursiveBright = nullptr,
	ImageAndView const* aPortalRecursiveNormal = nullptr,
	ImageAndView const* aPortalRecursiveDepth = nullptr,
	VkPipeline aPortalSurfacePipe = VK_NULL_HANDLE,
	VkDescriptorSet aPortalSurfaceDesc = VK_NULL_HANDLE,
	VkDescriptorSet aPortalRecursiveSurfaceDesc = VK_NULL_HANDLE,
	uint32_t aPortalMeshIndex = UINT32_MAX,
	glm::mat4 const* aPortalSurfaceTransform = nullptr,

	bool aPortal2Enabled = false,
	glsl::SceneUniform const* aPortal2SceneUniform = nullptr,
	glsl::SceneUniform const* aPortal2RecursiveSceneUniforms = nullptr,
	uint32_t aPortal2RecursiveSceneUniformCount = 0,
	const std::vector<RenderBatch>* aPortal2Batches = nullptr,
	ImageAndView const* aPortal2Color = nullptr,
	ImageAndView const* aPortal2Bright = nullptr,
	ImageAndView const* aPortal2Normal = nullptr,
	ImageAndView const* aPortal2Depth = nullptr,
	ImageAndView const* aPortal2RecursiveColor = nullptr,
	ImageAndView const* aPortal2RecursiveBright = nullptr,
	ImageAndView const* aPortal2RecursiveNormal = nullptr,
	ImageAndView const* aPortal2RecursiveDepth = nullptr,
	VkDescriptorSet aPortal2SurfaceDesc = VK_NULL_HANDLE,
	VkDescriptorSet aPortal2RecursiveSurfaceDesc = VK_NULL_HANDLE,
	glm::mat4 const* aPortal2SurfaceTransform = nullptr,
	float aPortalEffectTime = 0.0f,
	bool aPortalMainVisible = false,
	bool aPortal2MainVisible = false,
	bool aPortalVisibleInPortalView = false,
	bool aPortalVisibleInPortal2View = false,
	bool aPortal2VisibleInPortalView = false,
	bool aPortal2VisibleInPortal2View = false
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
