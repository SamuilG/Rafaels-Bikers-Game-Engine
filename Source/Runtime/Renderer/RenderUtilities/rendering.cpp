#include "rendering.hpp"

#include "../../Rhi/commands.hpp"
#include "../../Rhi/synch.hpp"
#include "../../Rhi/error.hpp"
#include "../../Rhi/to_string.hpp"
#include "setup.hpp"	

#include <glm/glm.hpp>
#include <glm/gtx/transform.hpp>
#include <glm/gtc/matrix_transform.hpp>

// multi-pass rendering
// render scene to offscreen image
// apply post processing and render to swapchain
// function definition

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
	VkClearColorValue aClearColor,
	float aBloomStrength,
	// --- 原有后处理与阴影/粒子参数保留 ---
	VkPipeline aPostProcPipe,
	VkDescriptorSet aPostProcDescriptors,
	VkPipelineLayout aPostProcLayout,
	VkPipeline aShadowPipe,
	ImageAndView const& aShadowMap,
	std::vector<VkImageView> const& aShadowCascadeViews,
	bool particlesEnabled,
	VkPipeline particlePipe,
	const std::vector<std::unique_ptr<ParticleSystem>>& allParticles
)
{
	// Begin command buffer
	VkCommandBufferBeginInfo beginInfo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
	beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	beginInfo.pInheritanceInfo = nullptr;

	if (auto const res = vkBeginCommandBuffer(aCmdBuff, &beginInfo); VK_SUCCESS != res)
	{
		throw lut::Error("Unable to begin recording command buffer\n"
			"vkBeginCommandBuffer() returned {}", lut::to_string(res)
		);
	}

	// Upload scene UBO (with barriers)
	lut::buffer_barrier(aCmdBuff, aSceneUBO, VK_PIPELINE_STAGE_VERTEX_SHADER_BIT, VK_ACCESS_UNIFORM_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT);
	vkCmdUpdateBuffer(aCmdBuff, aSceneUBO, 0, sizeof(glsl::SceneUniform), &aSceneUniform);
	lut::buffer_barrier(aCmdBuff, aSceneUBO, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_VERTEX_SHADER_BIT, VK_ACCESS_UNIFORM_READ_BIT);

	// ---------------------------
	// Shadow pass (保持原实现)
	// ---------------------------
	{
		lut::image_barrier(aCmdBuff, aShadowMap.image,
			VK_PIPELINE_STAGE_2_NONE,
			VK_ACCESS_2_NONE,
			VK_IMAGE_LAYOUT_UNDEFINED,
			VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT,
			VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
			VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
			VkImageSubresourceRange{ VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, kCascadeCount }
		);

		for (uint32_t i = 0; i < kCascadeCount; ++i)
		{
			VkImageView currentLayerView = aShadowCascadeViews[i];

			VkRenderingAttachmentInfo shadowDepthInfo{};
			shadowDepthInfo.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
			shadowDepthInfo.imageView = currentLayerView;
			shadowDepthInfo.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
			shadowDepthInfo.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
			shadowDepthInfo.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
			shadowDepthInfo.clearValue.depthStencil = { 1.f, 0 };

			VkRenderingInfo shadowRenderInfo{};
			shadowRenderInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
			shadowRenderInfo.renderArea.offset = { 0, 0 };
			shadowRenderInfo.renderArea.extent = { kShadowMapResolution, kShadowMapResolution };
			shadowRenderInfo.layerCount = 1;
			shadowRenderInfo.pDepthAttachment = &shadowDepthInfo;

			vkCmdBeginRendering(aCmdBuff, &shadowRenderInfo);
			vkCmdBindPipeline(aCmdBuff, VK_PIPELINE_BIND_POINT_GRAPHICS, aShadowPipe);

			VkViewport shadowVp{ 0.f, 0.f, float(kShadowMapResolution), float(kShadowMapResolution), 0.f, 1.f };
			vkCmdSetViewport(aCmdBuff, 0, 1, &shadowVp);
			VkRect2D shadowScissor{ {0,0}, {kShadowMapResolution, kShadowMapResolution} };
			vkCmdSetScissor(aCmdBuff, 0, 1, &shadowScissor);

			// 深度偏移（可调）
			vkCmdSetDepthBias(aCmdBuff, 1.5f, 0.f, 1.85f);

			vkCmdBindDescriptorSets(aCmdBuff, VK_PIPELINE_BIND_POINT_GRAPHICS, aGraphicsLayout, 0, 1, &aSceneDescriptors, 0, nullptr);

			VkDeviceSize kZeroOffset = 0;
			for (const auto& batch : aBatches) {
				uint32_t meshIdx = batch.meshIndex;
				uint32_t matIdx = batch.materialIndex;

				glm::mat4 lightModel = aSceneUniform.lightVP[i] * batch.transform;
				vkCmdPushConstants(aCmdBuff, aGraphicsLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(glm::mat4), &lightModel);

				vkCmdBindDescriptorSets(aCmdBuff, VK_PIPELINE_BIND_POINT_GRAPHICS, aGraphicsLayout, 1, 1, &aMaterialDescriptors[matIdx], 0, nullptr);

				vkCmdBindVertexBuffers(aCmdBuff, 0, 1, &aMeshPositions[meshIdx].buffer, &kZeroOffset);
				vkCmdBindVertexBuffers(aCmdBuff, 1, 1, &aMeshTexCoords[meshIdx].buffer, &kZeroOffset);
				vkCmdBindIndexBuffer(aCmdBuff, aMeshIndices[meshIdx].buffer, 0, VK_INDEX_TYPE_UINT32);

				vkCmdDrawIndexed(aCmdBuff, static_cast<uint32_t>(aMeshInfos[meshIdx].indices.size()), 1, 0, 0, 0);
			}
			vkCmdEndRendering(aCmdBuff);
		}

		lut::image_barrier(aCmdBuff, aShadowMap.image,
			VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
			VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
			VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
			VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
			VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
			VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL,
			VkImageSubresourceRange{ VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, kCascadeCount }
		);
	}

	// ==========================================================
	// PASS 1: 场景渲染 + 亮度提取 (MRT -> Offscreen + Bright)
	// ==========================================================
	// Transition offscreen + bright targets to color attachment layouts
	lut::image_barrier(aCmdBuff, aOffscreenColor.image,
		VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
		VK_ACCESS_2_NONE,
		VK_IMAGE_LAYOUT_UNDEFINED,
		VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
		VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
		VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
		VkImageSubresourceRange{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 }
	);
	lut::image_barrier(aCmdBuff, aBrightColor.image,
		VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
		VK_ACCESS_2_NONE,
		VK_IMAGE_LAYOUT_UNDEFINED,
		VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
		VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
		VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
		VkImageSubresourceRange{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 }
	);

	// Depth attachment for this pass
	VkRenderingAttachmentInfo depthAttachmentInfo{};
	depthAttachmentInfo.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
	depthAttachmentInfo.imageView = aDepthAttach.view;
	depthAttachmentInfo.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
	depthAttachmentInfo.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	depthAttachmentInfo.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	depthAttachmentInfo.clearValue.depthStencil = { 1.f, 0 };

	VkRenderingAttachmentInfo colorAtts[2]{};

	// 显式配置附件 0
	colorAtts[0].sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
	colorAtts[0].imageView = aOffscreenColor.view;
	colorAtts[0].imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	colorAtts[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	colorAtts[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	colorAtts[0].clearValue.color = aClearColor;

	// 显式配置附件 1（亮度提取图）
	colorAtts[1].sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
	colorAtts[1].imageView = aBrightColor.view;
	colorAtts[1].imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	colorAtts[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR; // 确保清除旧数据
	colorAtts[1].storeOp = VK_ATTACHMENT_STORE_OP_STORE; // ！！！关键：必须确保为 STORE ！！！
	colorAtts[1].clearValue.color = { 0.f, 0.f, 0.f, 1.f };

	VkRenderingInfo mrtInfo{};
	mrtInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
	mrtInfo.renderArea.offset = { 0, 0 };
	mrtInfo.renderArea.extent = aImageExtent;
	mrtInfo.layerCount = 1;
	mrtInfo.colorAttachmentCount = 2;
	mrtInfo.pColorAttachments = colorAtts;
	mrtInfo.pDepthAttachment = &depthAttachmentInfo;

	vkCmdBeginRendering(aCmdBuff, &mrtInfo);

	// Bind pipelines / descriptors and draw scene as before (render to offscreen + bright)
	vkCmdBindPipeline(aCmdBuff, VK_PIPELINE_BIND_POINT_GRAPHICS, aGraphicsPipe);
	vkCmdBindDescriptorSets(aCmdBuff, VK_PIPELINE_BIND_POINT_GRAPHICS, aGraphicsLayout, 0, 1, &aSceneDescriptors, 0, nullptr);

	// Viewport / scissor
	VkViewport vp{};
	vp.x = 0.f; vp.y = 0.f;
	vp.width = float(aImageExtent.width); vp.height = float(aImageExtent.height);
	vp.minDepth = 0.f; vp.maxDepth = 1.f;
	vkCmdSetViewport(aCmdBuff, 0, 1, &vp);
	VkRect2D scissor{ {0,0}, aImageExtent };
	vkCmdSetScissor(aCmdBuff, 0, 1, &scissor);

	VkPipeline currentPipeline = aGraphicsPipe;
	VkDeviceSize kZeroOffset = 0;

	for (const auto& batch : aBatches)
	{
		uint32_t meshIdx = batch.meshIndex;
		uint32_t matIdx = batch.materialIndex;
		auto const& meshInfo = aMeshInfos[meshIdx];

		vkCmdPushConstants(aCmdBuff, aGraphicsLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(glm::mat4), &batch.transform);

		VkPipeline targetPipeline = aGraphicsPipe;
		if (matIdx < aMaterials.size() && aMaterials[matIdx].alphaMaskTexture >= 0)
			targetPipeline = aAlphaPipe;

		if (targetPipeline != currentPipeline) {
			vkCmdBindPipeline(aCmdBuff, VK_PIPELINE_BIND_POINT_GRAPHICS, targetPipeline);
			currentPipeline = targetPipeline;
		}

		vkCmdBindDescriptorSets(aCmdBuff, VK_PIPELINE_BIND_POINT_GRAPHICS, aGraphicsLayout, 1, 1, &aMaterialDescriptors[matIdx], 0, nullptr);

		vkCmdBindVertexBuffers(aCmdBuff, 0, 1, &aMeshPositions[meshIdx].buffer, &kZeroOffset);
		vkCmdBindVertexBuffers(aCmdBuff, 1, 1, &aMeshTexCoords[meshIdx].buffer, &kZeroOffset);
		vkCmdBindVertexBuffers(aCmdBuff, 2, 1, &aMeshNormals[meshIdx].buffer, &kZeroOffset);
		vkCmdBindIndexBuffer(aCmdBuff, aMeshIndices[meshIdx].buffer, 0, VK_INDEX_TYPE_UINT32);

		vkCmdDrawIndexed(aCmdBuff, static_cast<uint32_t>(meshInfo.indices.size()), 1, 0, 0, 0);
	}

	// Particles (保持原逻辑)
	if (particlesEnabled)
	{
		vkCmdBindPipeline(aCmdBuff, VK_PIPELINE_BIND_POINT_GRAPHICS, particlePipe);
		vkCmdBindDescriptorSets(aCmdBuff, VK_PIPELINE_BIND_POINT_GRAPHICS, aGraphicsLayout, 0, 1, &aSceneDescriptors, 0, nullptr);

		struct ParticlePC { int useTexture; int debugMode; int _pad[2]; glm::mat4 transform; };

		for (const auto& ps : allParticles)
		{
			ParticlePC pc{};
			if (ps->config.textureDescriptor != VK_NULL_HANDLE) {
				vkCmdBindDescriptorSets(aCmdBuff, VK_PIPELINE_BIND_POINT_GRAPHICS, aGraphicsLayout, 1, 1, &ps->config.textureDescriptor, 0, nullptr);
				pc.useTexture = ps->config.useTexture;
			}
			else {
				pc.useTexture = 0;
			}
			pc.debugMode = ps->config.particleDebug ? 1 : 0;
			vkCmdPushConstants(aCmdBuff, aGraphicsLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(ParticlePC), &pc);
			ps->draw(aCmdBuff);
			ps->drawDebug(aCmdBuff, aGraphicsLayout);
		}
	}

	vkCmdEndRendering(aCmdBuff);

	// ==========================================================
	// Blur pass: Horizontal (Bright -> BlurTemp)
	// ==========================================================
	lut::image_barrier(aCmdBuff, aBrightColor.image,
		VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
		VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
		VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
		VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
		VK_ACCESS_2_SHADER_READ_BIT,
		VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		VkImageSubresourceRange{ VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1 }
	);
	lut::image_barrier(aCmdBuff, aBlurTemp.image,
		VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
		VK_ACCESS_2_NONE,
		VK_IMAGE_LAYOUT_UNDEFINED,
		VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
		VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
		VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
		VkImageSubresourceRange{ VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1 }
	);

	VkRenderingAttachmentInfo blurAtt{};
	blurAtt.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
	blurAtt.imageView = aBlurTemp.view;
	blurAtt.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	blurAtt.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	blurAtt.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

	VkRenderingInfo blurRenderInfo{};
	blurRenderInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
	blurRenderInfo.renderArea.offset = { 0,0 };
	blurRenderInfo.renderArea.extent = aImageExtent;
	blurRenderInfo.layerCount = 1;
	blurRenderInfo.colorAttachmentCount = 1;
	blurRenderInfo.pColorAttachments = &blurAtt;

	vkCmdBeginRendering(aCmdBuff, &blurRenderInfo);
	vkCmdBindPipeline(aCmdBuff, VK_PIPELINE_BIND_POINT_GRAPHICS, aBlurPipe);
	vkCmdBindDescriptorSets(aCmdBuff, VK_PIPELINE_BIND_POINT_GRAPHICS, aBlurLayout, 0, 1, &aBlurHorizDS, 0, nullptr);
	int horizontal = 1;
	vkCmdPushConstants(aCmdBuff, aBlurLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(int), &horizontal);
	vkCmdSetViewport(aCmdBuff, 0, 1, &vp);
	vkCmdSetScissor(aCmdBuff, 0, 1, &scissor);
	vkCmdDraw(aCmdBuff, 3, 1, 0, 0);
	vkCmdEndRendering(aCmdBuff);

	// ==========================================================
	// Blur pass: Vertical (BlurTemp -> FinalBloom)
	// ==========================================================
	lut::image_barrier(aCmdBuff, aBlurTemp.image,
		VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
		VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
		VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
		VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
		VK_ACCESS_2_SHADER_READ_BIT,
		VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		VkImageSubresourceRange{ VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1 }
	);
	lut::image_barrier(aCmdBuff, aFinalBloom.image,
		VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
		VK_ACCESS_2_NONE,
		VK_IMAGE_LAYOUT_UNDEFINED,
		VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
		VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
		VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
		VkImageSubresourceRange{ VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1 }
	);

	blurAtt.imageView = aFinalBloom.view;
	vkCmdBeginRendering(aCmdBuff, &blurRenderInfo);
	vkCmdBindDescriptorSets(aCmdBuff, VK_PIPELINE_BIND_POINT_GRAPHICS, aBlurLayout, 0, 1, &aBlurVertDS, 0, nullptr);
	horizontal = 0;
	vkCmdPushConstants(aCmdBuff, aBlurLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(int), &horizontal);
	vkCmdDraw(aCmdBuff, 3, 1, 0, 0);
	vkCmdEndRendering(aCmdBuff);

	// ==========================================================
	// Composite: combine Offscreen + FinalBloom -> Swapchain
	// ==========================================================
	lut::image_barrier(aCmdBuff, aOffscreenColor.image,
		VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
		VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
		VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
		VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
		VK_ACCESS_2_SHADER_READ_BIT,
		VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		VkImageSubresourceRange{ VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1 }
	);
	lut::image_barrier(aCmdBuff, aFinalBloom.image,
		VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
		VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
		VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
		VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
		VK_ACCESS_2_SHADER_READ_BIT,
		VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		VkImageSubresourceRange{ VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1 }
	);
	lut::image_barrier(aCmdBuff, aSwapchainAttach.image,
		VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
		VK_ACCESS_2_NONE,
		VK_IMAGE_LAYOUT_UNDEFINED,
		VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
		VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
		VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
		VkImageSubresourceRange{ VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1 }
	);

	VkRenderingAttachmentInfo compAtt{};
	compAtt.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
	compAtt.imageView = aSwapchainAttach.view;
	compAtt.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	compAtt.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	compAtt.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

	VkRenderingInfo compInfo{};
	compInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
	compInfo.renderArea.offset = { 0,0 };
	compInfo.renderArea.extent = aImageExtent;
	compInfo.layerCount = 1;
	compInfo.colorAttachmentCount = 1;
	compInfo.pColorAttachments = &compAtt;

	vkCmdBeginRendering(aCmdBuff, &compInfo);
	vkCmdBindPipeline(aCmdBuff, VK_PIPELINE_BIND_POINT_GRAPHICS, aCompositePipe);
	// 【关键修复】：将 aBlurLayout 替换为 aCompositeLayout
	vkCmdBindDescriptorSets(aCmdBuff, VK_PIPELINE_BIND_POINT_GRAPHICS, aCompositeLayout, 0, 1, &aCompositeDS, 0, nullptr);
	// bloom 参数 (示例)
	// 【修改】：使用传进来的 aBloomStrength
	struct BloomPC { float exposure; float strength; float _pad[2]; } bloomPC{ 1.0f, aBloomStrength, {0.0f,0.0f} };
	vkCmdPushConstants(aCmdBuff, aCompositeLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(BloomPC), &bloomPC);


	vkCmdSetViewport(aCmdBuff, 0, 1, &vp);
	vkCmdSetScissor(aCmdBuff, 0, 1, &scissor);
	vkCmdDraw(aCmdBuff, 3, 1, 0, 0);
	vkCmdEndRendering(aCmdBuff);

	// Transition swapchain for present
	lut::image_barrier(aCmdBuff, aSwapchainAttach.image,
		VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
		VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
		VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
		VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT,
		VK_ACCESS_2_NONE,
		VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
		VkImageSubresourceRange{ VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1 }
	);

	if (auto const res = vkEndCommandBuffer(aCmdBuff); VK_SUCCESS != res)
	{
		throw lut::Error("Unable to end recording command buffer\n"
			"vkEndCommandBuffer() returned {}", lut::to_string(res)
		);
	}
}
void submit_commands(lut::VulkanContext const& aContext, VkCommandBuffer aCmdBuff, VkFence aFence, VkSemaphore aWaitSemaphore, VkSemaphore aSignalSemaphore)
{
	VkSemaphoreSubmitInfo wait[1]{};
	wait[0].sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
	wait[0].semaphore = aWaitSemaphore;
	wait[0].stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;

	VkSemaphoreSubmitInfo signal[1]{};
	signal[0].sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
	signal[0].semaphore = aSignalSemaphore;
	signal[0].stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;

	VkCommandBufferSubmitInfo submit[1]{};
	submit[0].sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
	submit[0].commandBuffer = aCmdBuff;

	VkSubmitInfo2 submitInfo{};
	submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
	submitInfo.waitSemaphoreInfoCount = 1;
	submitInfo.pWaitSemaphoreInfos = wait;
	submitInfo.commandBufferInfoCount = 1;
	submitInfo.pCommandBufferInfos = submit;
	submitInfo.signalSemaphoreInfoCount = 1;
	submitInfo.pSignalSemaphoreInfos = signal;

	if (auto const res = vkQueueSubmit2(aContext.graphicsQueue, 1, &submitInfo, aFence); VK_SUCCESS != res)
	{
		throw lut::Error("Unable to submit command buffer to queue\n"
			"vkQueueSubmit2() returned {}", lut::to_string(res)
		);
	}
}

void present_results(VkQueue aPresentQueue, VkSwapchainKHR aSwapchain, std::uint32_t aImageIndex, VkSemaphore aRenderFinished, bool& aNeedToRecreateSwapchain)
{
	VkPresentInfoKHR presentInfo{};
	presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
	presentInfo.waitSemaphoreCount = 1;
	presentInfo.pWaitSemaphores = &aRenderFinished;
	presentInfo.swapchainCount = 1;
	presentInfo.pSwapchains = &aSwapchain;
	presentInfo.pImageIndices = &aImageIndex;
	presentInfo.pResults = nullptr;

	auto const presentRes = vkQueuePresentKHR(aPresentQueue, &presentInfo);

	if (VK_SUBOPTIMAL_KHR == presentRes || VK_ERROR_OUT_OF_DATE_KHR == presentRes)
	{
		aNeedToRecreateSwapchain = true;
	}
	else if (VK_SUCCESS != presentRes)
	{
		throw lut::Error("Unable present swapchain image {}\n"
			"vkQueuePresentKHR() returned {}", aImageIndex, lut::to_string(presentRes)
		);
	}
}