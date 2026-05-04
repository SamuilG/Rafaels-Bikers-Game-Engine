#include "rendering.hpp"

#include "../../Rhi/commands.hpp"
#include "../../Rhi/synch.hpp"
#include "../../Rhi/error.hpp"
#include "../../Rhi/to_string.hpp"
#include "setup.hpp"	

#include <glm/glm.hpp>
#include <glm/gtx/transform.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "../../UI/ui.hpp"

// multi-pass rendering
// render scene to offscreen image
// apply post processing and render to swapchain
// function definition

// 在 rendering.cpp 顶部或者 record_commands 函数外定义：
// 严格对齐 Shader 的 PushConstants 布局
struct alignas(16) ObjectPC {
	glm::mat4 transform;        // Offset: 0   Size: 64
	glm::vec4 baseColorFactor;  // Offset: 64  Size: 16
	glm::vec4 emissiveFactor;   // Offset: 80  Size: 16  <-- 补上这个，方块就不绿了
	float metallicFactor;       // Offset: 96  Size: 4
	float roughnessFactor;      // Offset: 100 Size: 4
	float alphaCutoff;          // Offset: 104 Size: 4
	float _pad;                 // Offset: 108 Size: 4  <-- 补齐到 16 字节边界
};

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
	VkPipeline aSkinnedPipe,
	VkPipeline aSkinnedAlphaPipe,
	VkPipelineLayout aSkinnedPipeLayout,
	VkDescriptorSet  aBoneDescriptorSet,
	const std::unordered_map<uint32_t, lut::Buffer>* aMeshJoints,
	const std::unordered_map<uint32_t, lut::Buffer>* aMeshWeights,
	const std::vector<RenderBatch>* aSkinnedBatches,
	VkPipeline aSkinnedShadowPipe,
	// 在 record_commands 的最后追加：
	VkPipeline skyboxPipe,
	VkPipelineLayout skyboxPipeLayout,
	VkDescriptorSet skyboxDescSet,
	VkBuffer skyboxVBO
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
				// 【新增】：如果这个批次被标记为不投射阴影（比如它是发光体），直接跳过它的阴影绘制！
				if (!batch.castShadow) {
					continue;
				}
				uint32_t meshIdx = batch.meshIndex;
				uint32_t matIdx = batch.materialIndex;
				//if (batch.alphaMultiplier < 0.99f) continue;
				// 【关键】：这里一定要乘上 lightVP，算出投影空间矩阵！
				glm::mat4 lightModel = aSceneUniform.lightVP[i] * batch.transform;

				// 【关键】：这里只推送 64 字节 (sizeof(glm::mat4))！不要传整个 128 字节！
				vkCmdPushConstants(aCmdBuff, aGraphicsLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(glm::mat4), &lightModel);


				vkCmdBindDescriptorSets(aCmdBuff, VK_PIPELINE_BIND_POINT_GRAPHICS, aGraphicsLayout, 1, 1, &aMaterialDescriptors[matIdx], 0, nullptr);

				vkCmdBindVertexBuffers(aCmdBuff, 0, 1, &aMeshPositions[meshIdx].buffer, &kZeroOffset);
				vkCmdBindVertexBuffers(aCmdBuff, 1, 1, &aMeshTexCoords[meshIdx].buffer, &kZeroOffset);
				vkCmdBindIndexBuffer(aCmdBuff, aMeshIndices[meshIdx].buffer, 0, VK_INDEX_TYPE_UINT32);

				vkCmdDrawIndexed(aCmdBuff, static_cast<uint32_t>(aMeshInfos[meshIdx].indices.size()), 1, 0, 0, 0);
			}

			// --- Skinned shadow pass (character and other animated meshes) ---
			if (aSkinnedShadowPipe != VK_NULL_HANDLE &&
				aSkinnedBatches && !aSkinnedBatches->empty() &&
				aSkinnedPipeLayout != VK_NULL_HANDLE &&
				aBoneDescriptorSet != VK_NULL_HANDLE &&
				aMeshJoints && aMeshWeights)
			{
				struct ShadowSkinnedPC {
					glm::mat4 lightModel;    // = lightVP[cascade] * model (64 bytes)
					uint32_t  boneBaseIndex; // (4 bytes)
					uint32_t  _pad[3];       // (12 bytes padding)
				};

				vkCmdBindPipeline(aCmdBuff, VK_PIPELINE_BIND_POINT_GRAPHICS, aSkinnedShadowPipe);
				// Re-bind set 0 and bind set 2 (bones) using the skinned pipeline layout
				vkCmdBindDescriptorSets(aCmdBuff, VK_PIPELINE_BIND_POINT_GRAPHICS, aSkinnedPipeLayout, 0, 1, &aSceneDescriptors, 0, nullptr);
				vkCmdBindDescriptorSets(aCmdBuff, VK_PIPELINE_BIND_POINT_GRAPHICS, aSkinnedPipeLayout, 2, 1, &aBoneDescriptorSet, 0, nullptr);

				for (const auto& batch : *aSkinnedBatches)
				{
					uint32_t meshIdx = batch.meshIndex;
					uint32_t matIdx = batch.materialIndex;

					auto jIt = aMeshJoints->find(meshIdx);
					auto wIt = aMeshWeights->find(meshIdx);
					if (jIt == aMeshJoints->end() || wIt == aMeshWeights->end()) continue;

					ShadowSkinnedPC pc{};
					pc.lightModel = aSceneUniform.lightVP[i] * batch.transform;
					pc.boneBaseIndex = batch.boneBaseIndex;

					vkCmdPushConstants(aCmdBuff, aSkinnedPipeLayout,
						VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
						0, sizeof(ShadowSkinnedPC), &pc);

					vkCmdBindDescriptorSets(aCmdBuff, VK_PIPELINE_BIND_POINT_GRAPHICS, aSkinnedPipeLayout,
						1, 1, &aMaterialDescriptors[matIdx], 0, nullptr);

					vkCmdBindVertexBuffers(aCmdBuff, 0, 1, &aMeshPositions[meshIdx].buffer, &kZeroOffset);
					vkCmdBindVertexBuffers(aCmdBuff, 1, 1, &aMeshTexCoords[meshIdx].buffer, &kZeroOffset);
					vkCmdBindVertexBuffers(aCmdBuff, 2, 1, &aMeshNormals[meshIdx].buffer, &kZeroOffset);
					vkCmdBindVertexBuffers(aCmdBuff, 3, 1, &jIt->second.buffer, &kZeroOffset);
					vkCmdBindVertexBuffers(aCmdBuff, 4, 1, &wIt->second.buffer, &kZeroOffset);
					vkCmdBindIndexBuffer(aCmdBuff, aMeshIndices[meshIdx].buffer, 0, VK_INDEX_TYPE_UINT32);
					vkCmdDrawIndexed(aCmdBuff, static_cast<uint32_t>(aMeshInfos[meshIdx].indices.size()), 1, 0, 0, 0);
				}
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

	// 【新增补漏 1】：将法线缓冲重置为可写入状态
	lut::image_barrier(aCmdBuff, aNormalImage.image,
		VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, VK_ACCESS_2_NONE, VK_IMAGE_LAYOUT_UNDEFINED,
		VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
		VkImageSubresourceRange{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 }
	);

	// 【新增补漏 2】：将深度缓冲重置为可读写状态！(极其关键，否则深度测试全崩)
	lut::image_barrier(aCmdBuff, aDepthAttach.image,
		VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, VK_ACCESS_2_NONE, VK_IMAGE_LAYOUT_UNDEFINED,
		VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
		VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT,
		VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
		VkImageSubresourceRange{ VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1 }
	);


	VkRenderingAttachmentInfo depthAttachmentInfo{};
	depthAttachmentInfo.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
	depthAttachmentInfo.imageView = aDepthAttach.view;
	depthAttachmentInfo.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
	depthAttachmentInfo.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	depthAttachmentInfo.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	depthAttachmentInfo.clearValue.depthStencil = { 1.f, 0 };

	VkRenderingAttachmentInfo colorAtts[3]{};

	// 附件 0: Scene Color
	colorAtts[0].sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
	colorAtts[0].imageView = aOffscreenColor.view;
	colorAtts[0].imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	colorAtts[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	colorAtts[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	colorAtts[0].clearValue.color = aClearColor;

	// 附件 1: Bright Color
	colorAtts[1] = colorAtts[0];
	colorAtts[1].imageView = aBrightColor.view;
	colorAtts[1].clearValue.color = { 0.f, 0.f, 0.f, 1.f };

	// 【新增】附件 2: Normal Buffer
	colorAtts[2] = colorAtts[0];
	colorAtts[2].imageView = aNormalImage.view;
	colorAtts[2].clearValue.color = { 0.f, 0.f, 0.f, 0.f }; // 默认背景无法线且绝对光滑(0)

	VkRenderingInfo mrtInfo{ VK_STRUCTURE_TYPE_RENDERING_INFO };
	mrtInfo.renderArea.extent = aImageExtent;
	mrtInfo.layerCount = 1;
	mrtInfo.colorAttachmentCount = 3; // <-- 改为 3
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
	// 阶段 1：先画【实心】物体 (包括树叶 Mask)
	vkCmdBindPipeline(aCmdBuff, VK_PIPELINE_BIND_POINT_GRAPHICS, aGraphicsPipe);
	vkCmdBindDescriptorSets(aCmdBuff, VK_PIPELINE_BIND_POINT_GRAPHICS, aGraphicsLayout, 0, 1, &aSceneDescriptors, 0, nullptr);

	for (const auto& batch : aBatches)
	{
		uint32_t matIdx = batch.materialIndex;
		uint32_t meshIdx = batch.meshIndex;

		bool isMasked = (matIdx < aMaterials.size() && aMaterials[matIdx].alphaMaskTexture >= 0);

		if (!isMasked && batch.alphaMultiplier < 0.99f) {
			continue;
		}

		vkCmdBindPipeline(aCmdBuff, VK_PIPELINE_BIND_POINT_GRAPHICS, aGraphicsPipe);
		auto const& meshInfo = aMeshInfos[meshIdx];

		// --- 完全、干净地初始化 ObjectPC ---
		ObjectPC pcData{};
		pcData.transform = batch.transform;

		if (matIdx < aMaterials.size()) {
			pcData.baseColorFactor = aMaterials[matIdx].baseColorFactor;
			pcData.emissiveFactor = aMaterials[matIdx].emissiveFactor;
			pcData.metallicFactor = aMaterials[matIdx].metallicFactor;
			pcData.roughnessFactor = aMaterials[matIdx].roughnessFactor;
			pcData.alphaCutoff = aMaterials[matIdx].alphaCutoff;
		}
		else {
			// 安全回退：默认非金属、粗糙、无自发光
			pcData.baseColorFactor = glm::vec4(1.0f);
			pcData.emissiveFactor = glm::vec4(0.0f);
			pcData.metallicFactor = 0.0f;
			pcData.roughnessFactor = 0.8f;
			pcData.alphaCutoff = 0.5f;
		}

		pcData.baseColorFactor.a *= batch.alphaMultiplier;

		// 压入管线
		vkCmdPushConstants(aCmdBuff, aGraphicsLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(ObjectPC), &pcData);

		vkCmdBindDescriptorSets(aCmdBuff, VK_PIPELINE_BIND_POINT_GRAPHICS, aGraphicsLayout, 1, 1, &aMaterialDescriptors[matIdx], 0, nullptr);
		vkCmdBindVertexBuffers(aCmdBuff, 0, 1, &aMeshPositions[meshIdx].buffer, &kZeroOffset);
		vkCmdBindVertexBuffers(aCmdBuff, 1, 1, &aMeshTexCoords[meshIdx].buffer, &kZeroOffset);
		vkCmdBindVertexBuffers(aCmdBuff, 2, 1, &aMeshNormals[meshIdx].buffer, &kZeroOffset);

		vkCmdBindIndexBuffer(aCmdBuff, aMeshIndices[meshIdx].buffer, 0, VK_INDEX_TYPE_UINT32);
		vkCmdDrawIndexed(aCmdBuff, static_cast<uint32_t>(meshInfo.indices.size()), 1, 0, 0, 0);
	}
	// =====================================================================
	// 阶段 1.5：画【天空盒】 (此时利用 Early-Z 剔除被建筑挡住的天空像素，性能极高！)
	// =====================================================================
	if (skyboxPipe != VK_NULL_HANDLE && skyboxVBO != VK_NULL_HANDLE) {
		vkCmdBindPipeline(aCmdBuff, VK_PIPELINE_BIND_POINT_GRAPHICS, skyboxPipe);

		// 显式设置视口和裁剪区域
		VkViewport viewport{};
		viewport.x = 0.0f;
		viewport.y = 0.0f;
		viewport.width = (float)aImageExtent.width;
		viewport.height = (float)aImageExtent.height;
		viewport.minDepth = 0.0f;
		viewport.maxDepth = 1.0f;
		vkCmdSetViewport(aCmdBuff, 0, 1, &viewport);

		VkRect2D scissor{};
		scissor.offset = { 0, 0 };
		scissor.extent = aImageExtent;
		vkCmdSetScissor(aCmdBuff, 0, 1, &scissor);

		// 绑定天空盒的管线布局和描述符，占用 Set 0
		vkCmdBindDescriptorSets(aCmdBuff, VK_PIPELINE_BIND_POINT_GRAPHICS, skyboxPipeLayout, 0, 1, &skyboxDescSet, 0, nullptr);
		VkDeviceSize offsets[1] = { 0 };
		vkCmdBindVertexBuffers(aCmdBuff, 0, 1, &skyboxVBO, offsets);

		vkCmdDraw(aCmdBuff, 36, 1, 0, 0);
	}

	// =====================================================================
	// 【新增】：在 MRT 结束前，补上不透明的骨骼动画！
	// 这样它们才能把深度和法线写入 G-Buffer，参与 SSR 和 SSAO
	// =====================================================================
	if (aSkinnedPipe != VK_NULL_HANDLE && aSkinnedBatches && !aSkinnedBatches->empty() && aMeshJoints && aMeshWeights && aBoneDescriptorSet != VK_NULL_HANDLE)
	{
		struct alignas(16) SkinnedPC {
			glm::mat4 transform; glm::vec4 baseColorFactor; float metallicFactor; float roughnessFactor; uint32_t boneBaseIndex; float _pad;
		};
		vkCmdBindPipeline(aCmdBuff, VK_PIPELINE_BIND_POINT_GRAPHICS, aSkinnedPipe);
		vkCmdBindDescriptorSets(aCmdBuff, VK_PIPELINE_BIND_POINT_GRAPHICS, aSkinnedPipeLayout, 0, 1, &aSceneDescriptors, 0, nullptr);
		vkCmdBindDescriptorSets(aCmdBuff, VK_PIPELINE_BIND_POINT_GRAPHICS, aSkinnedPipeLayout, 2, 1, &aBoneDescriptorSet, 0, nullptr);

		for (const auto& batch : *aSkinnedBatches) {
			uint32_t meshIdx = batch.meshIndex; uint32_t matIdx = batch.materialIndex;
			// 只画不透明的
			bool isAlpha = (matIdx < aMaterials.size() && aMaterials[matIdx].alphaMaskTexture >= 0) || (batch.alphaMultiplier < 0.99f);
			if (isAlpha) continue;

			auto jIt = aMeshJoints->find(meshIdx); auto wIt = aMeshWeights->find(meshIdx);
			if (jIt == aMeshJoints->end() || wIt == aMeshWeights->end()) continue;

			SkinnedPC pc{};
			pc.transform = batch.transform; pc.boneBaseIndex = batch.boneBaseIndex;
			if (matIdx < aMaterials.size()) {
				pc.baseColorFactor = aMaterials[matIdx].baseColorFactor; pc.metallicFactor = aMaterials[matIdx].metallicFactor; pc.roughnessFactor = aMaterials[matIdx].roughnessFactor;
			}
			else {
				pc.baseColorFactor = glm::vec4(1.0f); pc.metallicFactor = pc.roughnessFactor = 1.0f;
			}

			vkCmdPushConstants(aCmdBuff, aSkinnedPipeLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(SkinnedPC), &pc);
			vkCmdBindDescriptorSets(aCmdBuff, VK_PIPELINE_BIND_POINT_GRAPHICS, aSkinnedPipeLayout, 1, 1, &aMaterialDescriptors[matIdx], 0, nullptr);

			VkDeviceSize z = 0;
			vkCmdBindVertexBuffers(aCmdBuff, 0, 1, &aMeshPositions[meshIdx].buffer, &z); vkCmdBindVertexBuffers(aCmdBuff, 1, 1, &aMeshTexCoords[meshIdx].buffer, &z);
			vkCmdBindVertexBuffers(aCmdBuff, 2, 1, &aMeshNormals[meshIdx].buffer, &z); vkCmdBindVertexBuffers(aCmdBuff, 3, 1, &jIt->second.buffer, &z); vkCmdBindVertexBuffers(aCmdBuff, 4, 1, &wIt->second.buffer, &z);
			vkCmdBindIndexBuffer(aCmdBuff, aMeshIndices[meshIdx].buffer, 0, VK_INDEX_TYPE_UINT32);
			vkCmdDrawIndexed(aCmdBuff, static_cast<uint32_t>(aMeshInfos[meshIdx].indices.size()), 1, 0, 0, 0);
		}
	}

	vkCmdEndRendering(aCmdBuff); // <---- 真正的 MRT 阶段结束！

	// ==========================================================
	// 准备进入 SSR 阶段：将刚刚画好的法线、深度、颜色转为只读采样状态
	// ==========================================================
	VkImageSubresourceRange range{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
	VkImageSubresourceRange depthRange{ VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1 };

	// 1. 法线 -> Read
	lut::image_barrier(aCmdBuff, aNormalImage.image,
		VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
		VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_READ_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, range);

	// 2. 场景颜色 -> Read (因为 SSR 要采原本的画面颜色当倒影)
	lut::image_barrier(aCmdBuff, aOffscreenColor.image,
		VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
		VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_READ_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, range);

	// 3. 深度图 -> Read (因为 SSR 要靠深度重建 3D 坐标)
	lut::image_barrier(aCmdBuff, aDepthAttach.image,
		VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT, VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
		VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_READ_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, depthRange);

	// 4. SSR Output -> Write (作为 SSR Pass 的画板)
	lut::image_barrier(aCmdBuff, aSsrOutput.image,
		VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, VK_ACCESS_2_NONE, VK_IMAGE_LAYOUT_UNDEFINED,
		VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, range);
	// ==========================================================
	// 执行 SSR Pass
	// ==========================================================
	VkRenderingAttachmentInfo ssrAtt{ VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
	ssrAtt.imageView = aSsrOutput.view;
	ssrAtt.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	// 顺手把这里改成 CLEAR，以防万一残留垃圾数据
	ssrAtt.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	ssrAtt.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	//ssrAtt.clearValue.color = { 0.0f, 0.0f, 0.0f, 0.0f }; // 清空为纯黑

	VkRenderingInfo ssrRenderInfo{ VK_STRUCTURE_TYPE_RENDERING_INFO };
	ssrRenderInfo.renderArea.extent = aImageExtent;
	ssrRenderInfo.layerCount = 1;
	ssrRenderInfo.colorAttachmentCount = 1;
	ssrRenderInfo.pColorAttachments = &ssrAtt;

	vkCmdBeginRendering(aCmdBuff, &ssrRenderInfo);
	// 【核心修改】：只有开启了 SSR，才给 GPU 下发绘制指令！
	if (aSsrEnabled) {
		vkCmdBindPipeline(aCmdBuff, VK_PIPELINE_BIND_POINT_GRAPHICS, aSsrPipe);
		vkCmdBindDescriptorSets(aCmdBuff, VK_PIPELINE_BIND_POINT_GRAPHICS, aSsrLayout, 0, 1, &aSsrDS, 0, nullptr);

		//float ssrParams[4] = { 0.2f /*步长*/, 100.0f /*最大步数*/, 0.1f /*厚度*/, 0.0f /*留白*/ };
		// 缩小步长让射线更密，增加厚度防止射线“穿模”漏判

		float ssrParams[4] = {
			0.08f,  /* 步长：稍微放宽一点点，配合 jittering 效果极佳 */
			128.0f, /* 最大步数 */
			0.25f,  /* 厚度：大幅收紧（之前是0.5），jittering 会防止漏判 */
			100.0f  /* 最大反射距离：放大到100，让远处的建筑也能倒映出来 */
		};
		vkCmdPushConstants(aCmdBuff, aSsrLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(float) * 4, ssrParams);
		vkCmdSetViewport(aCmdBuff, 0, 1, &vp);
		vkCmdSetScissor(aCmdBuff, 0, 1, &scissor);

		vkCmdDraw(aCmdBuff, 3, 1, 0, 0); // 触发全屏四边形
	}
	vkCmdEndRendering(aCmdBuff);
	// SSR 输出完毕，转为只读，留给 Composite 阶段去混合！
	lut::image_barrier(aCmdBuff, aSsrOutput.image,
		VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
		VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_READ_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, range);

	// ==========================================================
	// SSAO Pass (流程同 SSR，先转状态、再渲染、最后转回只读)
	// ==========================================================
	// ==========================================================
	// 执行 SSAO Pass
	// ==========================================================

	// 【修正】：只把 SSAO 的画板转为可写状态！不要碰 SSR！
	lut::image_barrier(aCmdBuff, aSsaoRawOutput.image,
		VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, VK_ACCESS_2_NONE, VK_IMAGE_LAYOUT_UNDEFINED,
		VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, range);

	VkRenderingAttachmentInfo ssaoAtt{ VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
	ssaoAtt.imageView = aSsaoRawOutput.view;
	ssaoAtt.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	ssaoAtt.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	ssaoAtt.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	// 默认清空为纯白 (1.0)，表示没有遮蔽
	ssaoAtt.clearValue.color = { 1.0f, 1.0f, 1.0f, 1.0f };

	VkRenderingInfo ssaoRenderInfo{ VK_STRUCTURE_TYPE_RENDERING_INFO };
	ssaoRenderInfo.renderArea.extent = aImageExtent;
	ssaoRenderInfo.layerCount = 1;
	ssaoRenderInfo.colorAttachmentCount = 1;
	ssaoRenderInfo.pColorAttachments = &ssaoAtt;

	vkCmdBeginRendering(aCmdBuff, &ssaoRenderInfo);
	if (aSsaoEnabled) {
		vkCmdBindPipeline(aCmdBuff, VK_PIPELINE_BIND_POINT_GRAPHICS, aSsaoPipe);
		vkCmdBindDescriptorSets(aCmdBuff, VK_PIPELINE_BIND_POINT_GRAPHICS, aSsaoLayout, 0, 1, &aSsaoDS, 0, nullptr);

		// 推送 SSAO 参数：[采样半径, 偏移容差, 阴影强度, 占位符]
		float ssaoParams[4] = { 1.5f, 0.15f, 3.0f, 0.0f };     // 新参数：增大半径，提高 Bias 过滤平地，提高对比度
		vkCmdPushConstants(aCmdBuff, aSsaoLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(float) * 4, ssaoParams);

		vkCmdSetViewport(aCmdBuff, 0, 1, &vp);
		vkCmdSetScissor(aCmdBuff, 0, 1, &scissor);

		vkCmdDraw(aCmdBuff, 3, 1, 0, 0);
	}
	vkCmdEndRendering(aCmdBuff);

	// SSAO 输出完毕，转为只读，准备给下个阶段（Blur 或者 Composite）使用
	lut::image_barrier(aCmdBuff, aSsaoRawOutput.image,
		VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
		VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_READ_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, range);


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
	// 为 Composite 画板准备屏障 (极其重要，防 Vulkan 报错)
	// ==========================================================
	//VkImageSubresourceRange range{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
	lut::image_barrier(aCmdBuff, aCompositeOutput.image,
		VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, VK_ACCESS_2_NONE, VK_IMAGE_LAYOUT_UNDEFINED,
		VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, range);

	// ==========================================================
	// 1. Composite Pass (终极合成阶段: 场景底色 + Bloom + 所有半透明物体)
	// ==========================================================

	// 【核心修复】：将深度图转为“只读深度测试”状态！
	lut::image_barrier(aCmdBuff, aDepthAttach.image,
		VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_READ_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
		VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
		VkImageSubresourceRange{ VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1 }
	);

	VkRenderingAttachmentInfo compDepthAtt{};
	compDepthAtt.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
	compDepthAtt.imageView = aDepthAttach.view;
	compDepthAtt.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
	compDepthAtt.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;   // 保留不透明物体的深度！
	compDepthAtt.storeOp = VK_ATTACHMENT_STORE_OP_NONE; // 半透明不写深度

	VkRenderingAttachmentInfo compAtt{};
	compAtt.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
	compAtt.imageView = aCompositeOutput.view; // 阶段 1 画到中转缓冲
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
	compInfo.pDepthAttachment = &compDepthAtt; // 挂载深度画板！

	vkCmdBeginRendering(aCmdBuff, &compInfo);

	// --- 1. 画全屏背景合成 (SSAO + SSR + 场景底色) ---
	vkCmdBindPipeline(aCmdBuff, VK_PIPELINE_BIND_POINT_GRAPHICS, aCompositePipe);
	vkCmdBindDescriptorSets(aCmdBuff, VK_PIPELINE_BIND_POINT_GRAPHICS, aCompositeLayout, 0, 1, &aCompositeDS, 0, nullptr);
	struct BloomPC { float exposure; float strength; float _pad[2]; } bloomPC{ 1.0f, aBloomStrength, {0.0f,0.0f} };
	vkCmdPushConstants(aCmdBuff, aCompositeLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(BloomPC), &bloomPC);
	vkCmdSetViewport(aCmdBuff, 0, 1, &vp);
	vkCmdSetScissor(aCmdBuff, 0, 1, &scissor);
	vkCmdDraw(aCmdBuff, 3, 1, 0, 0);

	// --- 2. 画半透明静态物体 ---
	vkCmdBindPipeline(aCmdBuff, VK_PIPELINE_BIND_POINT_GRAPHICS, aAlphaPipe);
	vkCmdBindDescriptorSets(aCmdBuff, VK_PIPELINE_BIND_POINT_GRAPHICS, aGraphicsLayout, 0, 1, &aSceneDescriptors, 0, nullptr);

	 kZeroOffset = 0; // 【修复 1】：补上类型声明
	for (const auto& batch : aBatches) {
		uint32_t meshIdx = batch.meshIndex; uint32_t matIdx = batch.materialIndex;
		bool isMatMasked = (matIdx < aMaterials.size() && aMaterials[matIdx].alphaMaskTexture >= 0);
		if (isMatMasked) continue;
		if (batch.alphaMultiplier >= 0.99f) continue;

		auto const& meshInfo = aMeshInfos[meshIdx];
		ObjectPC pcData{};
		pcData.transform = batch.transform;
		if (matIdx < aMaterials.size()) {
			pcData.baseColorFactor = aMaterials[matIdx].baseColorFactor; pcData.emissiveFactor = aMaterials[matIdx].emissiveFactor;
			pcData.metallicFactor = aMaterials[matIdx].metallicFactor; pcData.roughnessFactor = aMaterials[matIdx].roughnessFactor;
			pcData.alphaCutoff = aMaterials[matIdx].alphaCutoff;
		}
		else {
			pcData.baseColorFactor = glm::vec4(1.0f); pcData.emissiveFactor = glm::vec4(0.0f);
			pcData.metallicFactor = 0.0f; pcData.roughnessFactor = 0.8f; pcData.alphaCutoff = 0.5f;
		}
		pcData.baseColorFactor.a *= batch.alphaMultiplier;
		pcData.alphaCutoff = -1.0f;

		vkCmdPushConstants(aCmdBuff, aGraphicsLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(ObjectPC), &pcData);
		vkCmdBindDescriptorSets(aCmdBuff, VK_PIPELINE_BIND_POINT_GRAPHICS, aGraphicsLayout, 1, 1, &aMaterialDescriptors[matIdx], 0, nullptr);
		vkCmdBindVertexBuffers(aCmdBuff, 0, 1, &aMeshPositions[meshIdx].buffer, &kZeroOffset);
		vkCmdBindVertexBuffers(aCmdBuff, 1, 1, &aMeshTexCoords[meshIdx].buffer, &kZeroOffset);
		vkCmdBindVertexBuffers(aCmdBuff, 2, 1, &aMeshNormals[meshIdx].buffer, &kZeroOffset);
		vkCmdBindIndexBuffer(aCmdBuff, aMeshIndices[meshIdx].buffer, 0, VK_INDEX_TYPE_UINT32);
		vkCmdDrawIndexed(aCmdBuff, static_cast<uint32_t>(meshInfo.indices.size()), 1, 0, 0, 0);
	}

	// --- 3. 画半透明骨骼动画 ---
	if (aSkinnedAlphaPipe != VK_NULL_HANDLE && aSkinnedBatches && !aSkinnedBatches->empty() && aBoneDescriptorSet != VK_NULL_HANDLE) {
		struct alignas(16) SkinnedPC {
			glm::mat4 transform; glm::vec4 baseColorFactor; float metallicFactor; float roughnessFactor; uint32_t boneBaseIndex; float _pad;
		};
		vkCmdBindPipeline(aCmdBuff, VK_PIPELINE_BIND_POINT_GRAPHICS, aSkinnedAlphaPipe);
		vkCmdBindDescriptorSets(aCmdBuff, VK_PIPELINE_BIND_POINT_GRAPHICS, aSkinnedPipeLayout, 0, 1, &aSceneDescriptors, 0, nullptr);
		vkCmdBindDescriptorSets(aCmdBuff, VK_PIPELINE_BIND_POINT_GRAPHICS, aSkinnedPipeLayout, 2, 1, &aBoneDescriptorSet, 0, nullptr);

		for (const auto& batch : *aSkinnedBatches) {
			uint32_t meshIdx = batch.meshIndex; uint32_t matIdx = batch.materialIndex;
			bool isAlpha = (matIdx < aMaterials.size() && aMaterials[matIdx].alphaMaskTexture >= 0) || (batch.alphaMultiplier < 0.99f);
			if (!isAlpha) continue;

			auto jIt = aMeshJoints->find(meshIdx); auto wIt = aMeshWeights->find(meshIdx);
			if (jIt == aMeshJoints->end() || wIt == aMeshWeights->end()) continue;

			SkinnedPC pc{};
			pc.transform = batch.transform; pc.boneBaseIndex = batch.boneBaseIndex;
			if (matIdx < aMaterials.size()) {
				pc.baseColorFactor = aMaterials[matIdx].baseColorFactor; pc.metallicFactor = aMaterials[matIdx].metallicFactor; pc.roughnessFactor = aMaterials[matIdx].roughnessFactor;
			}
			else {
				pc.baseColorFactor = glm::vec4(1.0f); pc.metallicFactor = pc.roughnessFactor = 1.0f;
			}
			pc.baseColorFactor.a *= batch.alphaMultiplier;

			vkCmdPushConstants(aCmdBuff, aSkinnedPipeLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(SkinnedPC), &pc);
			vkCmdBindDescriptorSets(aCmdBuff, VK_PIPELINE_BIND_POINT_GRAPHICS, aSkinnedPipeLayout, 1, 1, &aMaterialDescriptors[matIdx], 0, nullptr);

			VkDeviceSize z = 0;
			vkCmdBindVertexBuffers(aCmdBuff, 0, 1, &aMeshPositions[meshIdx].buffer, &z); vkCmdBindVertexBuffers(aCmdBuff, 1, 1, &aMeshTexCoords[meshIdx].buffer, &z);
			vkCmdBindVertexBuffers(aCmdBuff, 2, 1, &aMeshNormals[meshIdx].buffer, &z); vkCmdBindVertexBuffers(aCmdBuff, 3, 1, &jIt->second.buffer, &z); vkCmdBindVertexBuffers(aCmdBuff, 4, 1, &wIt->second.buffer, &z);
			vkCmdBindIndexBuffer(aCmdBuff, aMeshIndices[meshIdx].buffer, 0, VK_INDEX_TYPE_UINT32);
			vkCmdDrawIndexed(aCmdBuff, static_cast<uint32_t>(aMeshInfos[meshIdx].indices.size()), 1, 0, 0, 0);
		}
	}

	// --- 4. 画粒子系统 ---
	if (particlesEnabled) {
		vkCmdBindPipeline(aCmdBuff, VK_PIPELINE_BIND_POINT_GRAPHICS, particlePipe);
		vkCmdBindDescriptorSets(aCmdBuff, VK_PIPELINE_BIND_POINT_GRAPHICS, aGraphicsLayout, 0, 1, &aSceneDescriptors, 0, nullptr);
		struct ParticlePC { int useTexture; int debugMode; int _pad[2]; glm::mat4 transform; };
		for (const auto& ps : allParticles) {
			if (!ps->config.isVisible) continue;
			ParticlePC pc{};
			if (ps->config.textureDescriptor != VK_NULL_HANDLE) {
				vkCmdBindDescriptorSets(aCmdBuff, VK_PIPELINE_BIND_POINT_GRAPHICS, aGraphicsLayout, 1, 1, &ps->config.textureDescriptor, 0, nullptr);
				pc.useTexture = ps->config.useTexture;
			}
			else { pc.useTexture = 0; }
			pc.debugMode = ps->config.particleDebug ? 1 : 0;
			vkCmdPushConstants(aCmdBuff, aGraphicsLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(ParticlePC), &pc);
			ps->draw(aCmdBuff);
			ps->drawDebug(aCmdBuff, aGraphicsLayout);
		}
	}

	// --- 5. 画调试线 ---
	aDebugRenderer.Render(aCmdBuff, aDebugLinePipe, aGraphicsLayout, aSceneDescriptors);

	// 最终合成渲染通道结束！
	vkCmdEndRendering(aCmdBuff);

	// ==========================================================
	// 2. 【解开注释并修复】：Speed Post-Process Pass 
	// ==========================================================
	// 将 aCompositeOutput (上一层的成品) 转换为只读，准备作为纹理被采样
	lut::image_barrier(aCmdBuff, aCompositeOutput.image,
		VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
		VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_READ_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, range);

	// 为 aFinalSceneColor (终极载体) 准备写入状态
	lut::image_barrier(aCmdBuff, aFinalSceneColor.image,
		VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, VK_ACCESS_2_NONE, VK_IMAGE_LAYOUT_UNDEFINED,
		VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, range);

	VkRenderingAttachmentInfo speedAtt{};
	speedAtt.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
	speedAtt.imageView = aFinalSceneColor.view;
	speedAtt.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	speedAtt.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	speedAtt.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

	VkRenderingInfo speedInfo{};
	speedInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
	speedInfo.renderArea.offset = { 0,0 };
	speedInfo.renderArea.extent = aImageExtent;
	speedInfo.layerCount = 1;
	speedInfo.colorAttachmentCount = 1;
	speedInfo.pColorAttachments = &speedAtt;

	vkCmdBeginRendering(aCmdBuff, &speedInfo);
	vkCmdBindPipeline(aCmdBuff, VK_PIPELINE_BIND_POINT_GRAPHICS, aSpeedPipe);
	vkCmdBindDescriptorSets(aCmdBuff, VK_PIPELINE_BIND_POINT_GRAPHICS, aSpeedLayout, 0, 1, &aSpeedDesc, 0, nullptr);

	vkCmdPushConstants(aCmdBuff, aSpeedLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(float), &aSpeedFactor);

	vkCmdSetViewport(aCmdBuff, 0, 1, &vp);
	vkCmdSetScissor(aCmdBuff, 0, 1, &scissor);
	vkCmdDraw(aCmdBuff, 3, 1, 0, 0);
	vkCmdEndRendering(aCmdBuff);

	// ==========================================================
	// 3. UI Render Pass: Render everything onto the actual Swapchain
	// ==========================================================

	// 转换 aFinalSceneColor 的状态，因为它现在是 3D 场景的载体，ImGui 需要把它当贴图读
	lut::image_barrier(aCmdBuff, aFinalSceneColor.image,
		VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
		VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_READ_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, range);

	// 准备物理屏幕 (Swapchain) 接收 UI 渲染
	lut::image_barrier(aCmdBuff, aSwapchainAttach.image,
		VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, VK_ACCESS_2_NONE, VK_IMAGE_LAYOUT_UNDEFINED,
		VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, range);

	VkRenderingAttachmentInfo uiColorAttach{ VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
	uiColorAttach.imageView = aSwapchainAttach.view; // 真正绘制到屏幕
	uiColorAttach.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	uiColorAttach.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	uiColorAttach.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	uiColorAttach.clearValue.color = { 0.12f, 0.12f, 0.12f, 1.0f }; // UI 背景板的底色，可改

	VkRenderingInfo uiRenderInfo{ VK_STRUCTURE_TYPE_RENDERING_INFO };
	uiRenderInfo.renderArea.offset = { 0, 0 };
	uiRenderInfo.renderArea.extent = aImageExtent;
	uiRenderInfo.layerCount = 1;
	uiRenderInfo.colorAttachmentCount = 1;
	uiRenderInfo.pColorAttachments = &uiColorAttach;

	vkCmdBeginRendering(aCmdBuff, &uiRenderInfo);

	// 真正触发 ImGui 的渲染！
	extern ImGuiRenderer imguiRenderer;
	imguiRenderer.Render(aCmdBuff);

	vkCmdEndRendering(aCmdBuff);

	// Transition swapchain for present
	lut::image_barrier(aCmdBuff, aSwapchainAttach.image,
		VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
		VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT, VK_ACCESS_2_NONE, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, range);

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