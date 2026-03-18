#include "rendering.hpp"

#include "../../Rhi/commands.hpp"
#include "../../Rhi/synch.hpp"
#include "../../Rhi/error.hpp"
#include "../../Rhi/to_string.hpp"
#include "setup.hpp"	

#include <glm/glm.hpp>
#include <glm/gtx/transform.hpp>
#include <glm/gtc/matrix_transform.hpp>
//UI system
#include "../../UI/ui.hpp"

// multi-pass rendering
// render scene to offscreen image
// apply post processing and render to swapchain
// function definition

void record_commands(VkCommandBuffer aCmdBuff, VkPipeline aGraphicsPipe, VkPipeline aAlphaPipe, ImageAndView const& aColorAttach, ImageAndView const& aDepthAttach, VkExtent2D const& aImageExtent, VkBuffer aSceneUBO, glsl::SceneUniform const& aSceneUniform, VkPipelineLayout aGraphicsLayout, VkDescriptorSet aSceneDescriptors, std::vector<lut::Buffer> const& aMeshPositions, std::vector<lut::Buffer> const& aMeshTexCoords, std::vector<lut::Buffer> const& aMeshNormals, std::vector<lut::Buffer> const& aMeshIndices, std::vector<EngineMesh> const& aMeshInfos, std::vector<EngineMaterial> const& aMaterials, std::vector<VkDescriptorSet> const& aMaterialDescriptors, std::vector<RenderBatch> const& aBatches, VkPipeline aPostProcPipe, VkDescriptorSet aPostProcDescriptors, VkPipelineLayout aPostProcLayout, ImageAndView const& aOffscreenColor, VkClearColorValue aClearColor, VkPipeline aShadowPipe, ImageAndView const& aShadowMap, std::vector<VkImageView> const& aShadowCascadeViews, bool particlesEnabled, VkPipeline particlePipe, const std::vector<std::unique_ptr<ParticleSystem>>& allParticles/*, VkDescriptorSet particleDescSet, VkBuffer particleVB, uint32_t particleCount*/)
{

	// begin recording commands
	VkCommandBufferBeginInfo beginInfo{};
	beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	beginInfo.pInheritanceInfo = nullptr;

	if (auto const res = vkBeginCommandBuffer(aCmdBuff, &beginInfo); VK_SUCCESS != res)
	{
		throw lut::Error("Unable to begin recording command buffer\n"
			"vkBeginCommandBuffer() returned {}", lut::to_string(res)
		);
	}

	// Upload scene uniforms
	lut::buffer_barrier(aCmdBuff, aSceneUBO, VK_PIPELINE_STAGE_VERTEX_SHADER_BIT, VK_ACCESS_UNIFORM_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT);

	vkCmdUpdateBuffer(aCmdBuff, aSceneUBO, 0, sizeof(glsl::SceneUniform), &aSceneUniform);

	lut::buffer_barrier(aCmdBuff, aSceneUBO, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_VERTEX_SHADER_BIT, VK_ACCESS_UNIFORM_READ_BIT);


	// p2_1.5 shadow pass
	
	{
		// 1. 全局 Barrier：将整个 Shadow Map Array 转换为深度附件布局
		lut::image_barrier(aCmdBuff, aShadowMap.image,
			VK_PIPELINE_STAGE_2_NONE,
			VK_ACCESS_2_NONE,
			VK_IMAGE_LAYOUT_UNDEFINED,
			VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT,
			VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
			VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
			VkImageSubresourceRange{ VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, kCascadeCount } 
		);

		
		// 2. 循环绘制每一个级联
		for (uint32_t i = 0; i < kCascadeCount; ++i)
		{
			VkImageView currentLayerView = aShadowCascadeViews[i]; // 使用传入的视图数组

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

			// 设置 Viewport/Scissor 为阴影图大小
			VkViewport viewport{ 0.f, 0.f, float(kShadowMapResolution), float(kShadowMapResolution), 0.f, 1.f };
			vkCmdSetViewport(aCmdBuff, 0, 1, &viewport);
			VkRect2D scissor{ {0,0}, {kShadowMapResolution, kShadowMapResolution} };
			vkCmdSetScissor(aCmdBuff, 0, 1, &scissor);

			vkCmdSetDepthBias(aCmdBuff, 1.25f, 0.f, 1.75f);

			// 绑定 Set 0 (包含 lightVP 数组的 UBO)
			vkCmdBindDescriptorSets(aCmdBuff, VK_PIPELINE_BIND_POINT_GRAPHICS, aGraphicsLayout, 0, 1, &aSceneDescriptors, 0, nullptr);

			VkDeviceSize kZeroOffset = 0;

			// Shadow push constant = lightVP[i] * world - same mat4 layout as main pass
			for (const auto& batch : aBatches) {
				uint32_t meshIdx = batch.meshIndex;
				uint32_t matIdx = batch.materialIndex;

				// Pre-multiply on CPU: no cascadeIndex in push constant needed
				glm::mat4 lightModel = aSceneUniform.lightVP[i] * batch.transform;
				vkCmdPushConstants(aCmdBuff, aGraphicsLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(glm::mat4), &lightModel);

				// bind material for alpha-masking (shadow pass respects alpha cutout)
				vkCmdBindDescriptorSets(aCmdBuff, VK_PIPELINE_BIND_POINT_GRAPHICS, aGraphicsLayout, 1, 1, &aMaterialDescriptors[matIdx], 0, nullptr);

				vkCmdBindVertexBuffers(aCmdBuff, 0, 1, &aMeshPositions[meshIdx].buffer, &kZeroOffset);
				vkCmdBindVertexBuffers(aCmdBuff, 1, 1, &aMeshTexCoords[meshIdx].buffer, &kZeroOffset);
				vkCmdBindIndexBuffer(aCmdBuff, aMeshIndices[meshIdx].buffer, 0, VK_INDEX_TYPE_UINT32);

				vkCmdDrawIndexed(aCmdBuff, static_cast<uint32_t>(aMeshInfos[meshIdx].indices.size()), 1, 0, 0, 0);
				// debug print first batch position
				if (!aBatches.empty()) {
					static bool printed = false;
					if (!printed) {
						std::printf("First Batch Matrix[3]: %f, %f, %f\n",
							aBatches[0].transform[3][0],
							aBatches[0].transform[3][1],
							aBatches[0].transform[3][2]);
						printed = true;
					}
				}
			}
			vkCmdEndRendering(aCmdBuff);
		}

		// 3. 结束后 Barrier：转换为着色器读取
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

	// render scene to offscreen image

	// transition offscreen image to color attachment optimal
	lut::image_barrier(aCmdBuff, aOffscreenColor.image,
		VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
		VK_ACCESS_2_NONE,
		VK_IMAGE_LAYOUT_UNDEFINED,
		VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
		VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
		VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
		VkImageSubresourceRange{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 }
	);

	// transition depth buffer to depth attachment optimal
	lut::image_barrier(aCmdBuff, aDepthAttach.image,
		VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
		VK_ACCESS_2_NONE,
		VK_IMAGE_LAYOUT_UNDEFINED,
		VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
		VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
		VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
		VkImageSubresourceRange{ VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1 }
	);

	// begin dynamic rendering for scene pass
	VkRenderingAttachmentInfo colorAttachment{};
	colorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
	colorAttachment.imageView = aOffscreenColor.view; // target offscreen
	colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	colorAttachment.clearValue.color = aClearColor;
	// colorAttachment.clearValue.color = { 0.1f, 0.1f, 0.1f, 1.f }; // Clear to black; not balck

	VkRenderingAttachmentInfo depthAttachment{};
	depthAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
	depthAttachment.imageView = aDepthAttach.view;
	depthAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
	depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	depthAttachment.clearValue.depthStencil = { 1.f, 0 };

	VkRenderingInfo renderInfo{};
	renderInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
	renderInfo.renderArea.offset = { 0, 0 };
	renderInfo.renderArea.extent = aImageExtent;
	renderInfo.layerCount = 1;
	renderInfo.colorAttachmentCount = 1;
	renderInfo.pColorAttachments = &colorAttachment;
	renderInfo.pDepthAttachment = &depthAttachment;

	vkCmdBeginRendering(aCmdBuff, &renderInfo);


	// Bind pipeline
	// Bind opaque pipeline initially
	vkCmdBindPipeline(aCmdBuff, VK_PIPELINE_BIND_POINT_GRAPHICS, aGraphicsPipe);
	vkCmdBindDescriptorSets(aCmdBuff, VK_PIPELINE_BIND_POINT_GRAPHICS, aGraphicsLayout, 0, 1, &aSceneDescriptors, 0, nullptr);

	// Set viewport/scissor
	VkViewport viewport{};
	viewport.x = 0.f;
	viewport.y = 0.f;
	viewport.width = float(aImageExtent.width);
	viewport.height = float(aImageExtent.height);
	viewport.minDepth = 0.f;
	viewport.maxDepth = 1.f;

	vkCmdSetViewport(aCmdBuff, 0, 1, &viewport);

	VkRect2D scissor{};
	scissor.offset = { 0, 0 };
	scissor.extent = aImageExtent;

	vkCmdSetScissor(aCmdBuff, 0, 1, &scissor);

	// draw scene geometry
	VkPipeline currentPipeline = aGraphicsPipe;
	VkDeviceSize kZeroOffset = 0;

	for (const auto& batch : aBatches) 
	{
		uint32_t meshIdx = batch.meshIndex;
		uint32_t matIdx = batch.materialIndex; // 获取该实例的材质
		auto const& meshInfo = aMeshInfos[meshIdx];
		

		// 推送变换矩阵
		vkCmdPushConstants(
			aCmdBuff,
			aGraphicsLayout,
			VK_SHADER_STAGE_VERTEX_BIT,
			0,
			sizeof(glm::mat4),
			&batch.transform
		);


		// task 1.6: select pipeline based on material
		VkPipeline targetPipeline = aGraphicsPipe;
		if (meshInfo.materialIndex < aMaterials.size() && aMaterials[meshInfo.materialIndex].alphaMaskTexture >= 0)
		{
			targetPipeline = aAlphaPipe;
		}

		if (targetPipeline != currentPipeline)
		{
			vkCmdBindPipeline(aCmdBuff, VK_PIPELINE_BIND_POINT_GRAPHICS, targetPipeline);
			currentPipeline = targetPipeline;
		}

		// bind object descriptor set
		// 绑定正确的材质描述符集 (解决灰色画面的核心)
		vkCmdBindDescriptorSets(
			aCmdBuff,
			VK_PIPELINE_BIND_POINT_GRAPHICS,
			aGraphicsLayout,
			1, 1,
			&aMaterialDescriptors[matIdx],
			0, nullptr
		);

		vkCmdBindVertexBuffers(aCmdBuff, 0, 1, &aMeshPositions[meshIdx].buffer, &kZeroOffset);
		vkCmdBindVertexBuffers(aCmdBuff, 1, 1, &aMeshTexCoords[meshIdx].buffer, &kZeroOffset);
		vkCmdBindVertexBuffers(aCmdBuff, 2, 1, &aMeshNormals[meshIdx].buffer, &kZeroOffset);
		vkCmdBindIndexBuffer(aCmdBuff, aMeshIndices[meshIdx].buffer, 0, VK_INDEX_TYPE_UINT32);

		vkCmdDrawIndexed(aCmdBuff, static_cast<uint32_t>(meshInfo.indices.size()), 1, 0, 0, 0);
	}

	if (particlesEnabled)
	{
		// ==========================================
		// 第一部分：循环外（所有粒子共用的全局状态）
		// ==========================================

		// 1. 绑定粒子的 Pipeline（只绑一次，效率最高）
		vkCmdBindPipeline(aCmdBuff, VK_PIPELINE_BIND_POINT_GRAPHICS, particlePipe);

		// 2. 绑定 Scene UBO (相机矩阵，所有粒子共用同一个相机，绑一次即可)
		vkCmdBindDescriptorSets(aCmdBuff, VK_PIPELINE_BIND_POINT_GRAPHICS, aGraphicsLayout, 0, 1, &aSceneDescriptors, 0, nullptr);

		// ==========================================
		// 第二部分：循环内（每个粒子系统专属的状态）
		// ==========================================

		// 推送常量结构体声明放在外面比较干净
		struct ParticlePC {
			int useTexture;
			int debugMode;      // 0 = 关闭红框，1 = 开启红框
			int _pad[2];        // 填充对齐
			glm::mat4 transform; // 占位，凑满 80 字节
		};

		// 遍历所有的粒子系统
		for (const auto& ps : allParticles)
		{
			ParticlePC pc{};

			// 1. 绑定该粒子专属的贴图 DescriptorSet (Set 1)
			if (ps->config.textureDescriptor != VK_NULL_HANDLE) {
				vkCmdBindDescriptorSets(aCmdBuff, VK_PIPELINE_BIND_POINT_GRAPHICS, aGraphicsLayout, 1, 1, &ps->config.textureDescriptor, 0, nullptr);
				pc.useTexture = ps->config.useTexture; // 根据配置决定是否使用贴图
			}
			else {
				pc.useTexture = 0; // 【安全保护】：如果没有分配贴图，强制关闭贴图模式，防止 Vulkan 崩溃
			}

			// 2. 读取该粒子的 Debug 配置
			pc.debugMode = ps->config.particleDebug ? 1 : 0;

			// 3. 上传推送常量
			vkCmdPushConstants(aCmdBuff, aGraphicsLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(ParticlePC), &pc);

			// 4. 绘制真实的粒子
			ps->draw(aCmdBuff);

			// 5. 绘制该粒子的 Debug 发射器线框
			ps->drawDebug(aCmdBuff, aGraphicsLayout);
		}
	}

	vkCmdEndRendering(aCmdBuff);

	// apply post processing and render to swapchain

	// transition offscreen image to shader read only
	lut::image_barrier(aCmdBuff, aOffscreenColor.image,
		VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
		VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
		VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
		VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
		VK_ACCESS_2_SHADER_READ_BIT,
		VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		VkImageSubresourceRange{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 }
	);

	// transition swapchain image to color attachment
	lut::image_barrier(aCmdBuff, aColorAttach.image,
		VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
		VK_ACCESS_2_NONE,
		VK_IMAGE_LAYOUT_UNDEFINED,
		VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
		VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
		VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
		VkImageSubresourceRange{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 }
	);

	VkRenderingAttachmentInfo postProcColorAttachment{};
	postProcColorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
	postProcColorAttachment.imageView = aColorAttach.view; // target swapchain
	postProcColorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	postProcColorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE; // full screen cover so clear is redundant
	postProcColorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

	VkRenderingInfo postProcRenderInfo{};
	postProcRenderInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
	postProcRenderInfo.renderArea.offset = { 0, 0 };
	postProcRenderInfo.renderArea.extent = aImageExtent;
	postProcRenderInfo.layerCount = 1;
	postProcRenderInfo.colorAttachmentCount = 1;
	postProcRenderInfo.pColorAttachments = &postProcColorAttachment;
	// no depth attachment for post process

	vkCmdBeginRendering(aCmdBuff, &postProcRenderInfo);

	vkCmdBindPipeline(aCmdBuff, VK_PIPELINE_BIND_POINT_GRAPHICS, aPostProcPipe);
	vkCmdBindDescriptorSets(aCmdBuff, VK_PIPELINE_BIND_POINT_GRAPHICS, aPostProcLayout, 0, 1, &aPostProcDescriptors, 0, nullptr);

	// viewport/scissor already set but reset for safety
	vkCmdSetViewport(aCmdBuff, 0, 1, &viewport);
	vkCmdSetScissor(aCmdBuff, 0, 1, &scissor);

	// draw full screen triangle
	vkCmdDraw(aCmdBuff, 3, 1, 0, 0);
	//==========UI system rendering===========
	imguiRenderer.Render(aCmdBuff);
	//==========UI system rendering===========

	vkCmdEndRendering(aCmdBuff);

	// transition swapchain image to present source
	lut::image_barrier(aCmdBuff, aColorAttach.image,
		VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
		VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
		VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
		VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT,
		VK_ACCESS_2_NONE,
		VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
		VkImageSubresourceRange{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 }
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