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
#include "../../UserState/UserState.hpp"

#include <algorithm>

struct alignas(16) PortalSurfacePC {
	glm::mat4 transform;
	glm::vec4 portalParams;
};

// multi-pass rendering
// render scene to offscreen image
// apply post processing and render to swapchain
// function definition

// 鍦?rendering.cpp 椤堕儴鎴栬€?record_commands 鍑芥暟澶栧畾涔夛細
// 涓ユ牸瀵归綈 Shader 鐨?PushConstants 甯冨眬
struct alignas(16) ObjectPC {
	glm::mat4 transform;        // Offset: 0   Size: 64
	glm::vec4 baseColorFactor;  // Offset: 64  Size: 16
	glm::vec4 emissiveFactor;   // Offset: 80  Size: 16  <-- 琛ヤ笂杩欎釜锛屾柟鍧楀氨涓嶇豢浜?
	float metallicFactor;       // Offset: 96  Size: 4
	float roughnessFactor;      // Offset: 100 Size: 4
	float alphaCutoff;          // Offset: 104 Size: 4
	float _pad;                 // Offset: 108 Size: 4
	glm::vec4 clipPlane;        // Offset: 112 Size: 16
};
// 鍦?rendering.cpp 椤堕儴瀹氫箟锛?
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
	ImageAndView const& aNormalImage,   // <--- 銆愭柊澧炪€戞硶绾跨紦鍐?
	ImageAndView const& aSsrOutput,     // <--- 銆愭柊澧炪€慡SR 缁撴灉杈撳嚭缂撳啿
	VkPipeline aSsrPipe,                // <--- 銆愭柊澧炪€?
	VkPipelineLayout aSsrLayout,        // <--- 銆愭柊澧炪€?
	VkDescriptorSet aSsrDS,             // <--- 銆愭柊澧炪€?
	// ==============================================================
	ImageAndView const& aSsaoRawOutput, // SSAO 鍘熷鍣偣鍥捐緭鍑虹洰鏍?
	VkPipeline aSsaoPipe,               // SSAO 娓叉煋绠＄嚎
	VkPipelineLayout aSsaoLayout,       // SSAO 绠＄嚎甯冨眬 (鐢ㄤ簬浼?PushConstants)
	VkDescriptorSet aSsaoDS,            // SSAO 鎻忚堪绗﹂泦 (缁戝畾浜嗘繁搴︺€佹硶绾裤€佸櫔澹拌创鍥惧拰閲囨牱鏍?
	bool aSsaoEnabled,                  // SSAO 寮€鍏?
	// ==============================================================
	bool aSsrEnabled,
	ImageAndView const& aBlurTemp,
	ImageAndView const& aFinalBloom,
	ImageAndView const& aCompositeOutput, // modified from aFinalSceneColor; used for bloom transfer
	VkClearColorValue aClearColor,
	float aBloomStrength,

	// ==============================================================
	// 鏋侀€熷悗澶勭悊鏁堟灉
	// ==============================================================
	VkPipeline aSpeedPipe,
	VkPipelineLayout aSpeedLayout,
	VkDescriptorSet aSpeedDesc,
	float aSpeedFactor,
	bool isAlive,       // <--- 鐩存帴鎶?userState 閲岀殑鍙橀噺鍠傜粰娓叉煋鍣紒
	float deathFactor,
	ImageAndView const& aFinalSceneColor, // 鏈€缁堝満鏅覆鏌撶紦鍐插尯锛堣緭鍑哄埌 ImGui锛?
	// ==============================================================

	// --- 鍘熸湁鍚庡鐞?闃村奖/绮掑瓙鍙傛暟 ---
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
	// 鍦?record_commands 鐨勬渶鍚庤拷鍔狅細
	VkPipeline skyboxPipe,
	VkPipelineLayout skyboxPipeLayout,
	VkDescriptorSet skyboxDescSet,
	VkBuffer skyboxVBO,
	bool aPortalEnabled,
	glsl::SceneUniform const* aPortalSceneUniform,
	glsl::SceneUniform const* aPortalRecursiveSceneUniforms,
	uint32_t aPortalRecursiveSceneUniformCount,
	const std::vector<RenderBatch>* aPortalBatches,
	ImageAndView const* aPortalColor,
	ImageAndView const* aPortalBright,
	ImageAndView const* aPortalNormal,
	ImageAndView const* aPortalDepth,
	ImageAndView const* aPortalRecursiveColor,
	ImageAndView const* aPortalRecursiveBright,
	ImageAndView const* aPortalRecursiveNormal,
	ImageAndView const* aPortalRecursiveDepth,
	VkPipeline aPortalSurfacePipe,
	VkDescriptorSet aPortalSurfaceDesc,
	VkDescriptorSet aPortalRecursiveSurfaceDesc,
	uint32_t aPortalMeshIndex,
	glm::mat4 const* aPortalSurfaceTransform,
	bool aPortal2Enabled,
	glsl::SceneUniform const* aPortal2SceneUniform,
	glsl::SceneUniform const* aPortal2RecursiveSceneUniforms,
	uint32_t aPortal2RecursiveSceneUniformCount,
	const std::vector<RenderBatch>* aPortal2Batches,
	ImageAndView const* aPortal2Color,
	ImageAndView const* aPortal2Bright,
	ImageAndView const* aPortal2Normal,
	ImageAndView const* aPortal2Depth,
	ImageAndView const* aPortal2RecursiveColor,
	ImageAndView const* aPortal2RecursiveBright,
	ImageAndView const* aPortal2RecursiveNormal,
	ImageAndView const* aPortal2RecursiveDepth,
	VkDescriptorSet aPortal2SurfaceDesc,
	VkDescriptorSet aPortal2RecursiveSurfaceDesc,
	glm::mat4 const* aPortal2SurfaceTransform,
	float aPortalEffectTime,
	bool aPortalMainVisible,
	bool aPortal2MainVisible,
	bool aPortalVisibleInPortalView,
	bool aPortalVisibleInPortal2View,
	bool aPortal2VisibleInPortalView,
	bool aPortal2VisibleInPortal2View
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

	auto uploadSceneUniform = [&](glsl::SceneUniform const& sceneUniform) {
		constexpr VkPipelineStageFlags kShaderStages = VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
		lut::buffer_barrier(aCmdBuff, aSceneUBO, kShaderStages, VK_ACCESS_UNIFORM_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT);
		vkCmdUpdateBuffer(aCmdBuff, aSceneUBO, 0, sizeof(glsl::SceneUniform), &sceneUniform);
		lut::buffer_barrier(aCmdBuff, aSceneUBO, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, kShaderStages, VK_ACCESS_UNIFORM_READ_BIT);
	};

	uploadSceneUniform(aSceneUniform);
	VkImageLayout shadowMapLayout = VK_IMAGE_LAYOUT_UNDEFINED;

	// ---------------------------
	// Shadow pass (淇濇寔鍘熷疄鐜?
	// ---------------------------
	auto renderShadowMap = [&](glsl::SceneUniform const& shadowSceneUniform,
		const std::vector<RenderBatch>& shadowBatches) {
		const VkPipelineStageFlags2 shadowOldStage =
			(shadowMapLayout == VK_IMAGE_LAYOUT_UNDEFINED)
			? VK_PIPELINE_STAGE_2_NONE
			: VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
		const VkAccessFlags2 shadowOldAccess =
			(shadowMapLayout == VK_IMAGE_LAYOUT_UNDEFINED)
			? VK_ACCESS_2_NONE
			: VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;

		lut::image_barrier(aCmdBuff, aShadowMap.image,
			shadowOldStage,
			shadowOldAccess,
			shadowMapLayout,
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

			// 娣卞害鍋忕Щ锛堝彲璋冿級
			vkCmdSetDepthBias(aCmdBuff, 1.5f, 0.f, 1.85f);

			vkCmdBindDescriptorSets(aCmdBuff, VK_PIPELINE_BIND_POINT_GRAPHICS, aGraphicsLayout, 0, 1, &aSceneDescriptors, 0, nullptr);

			VkDeviceSize kZeroOffset = 0;
			for (const auto& batch : shadowBatches) {
				// 銆愭柊澧炪€戯細濡傛灉杩欎釜鎵规琚爣璁颁负涓嶆姇灏勯槾褰憋紙姣斿瀹冩槸鍙戝厜浣擄級锛岀洿鎺ヨ烦杩囧畠鐨勯槾褰辩粯鍒讹紒
				if (!batch.castShadow) {
					continue;
				}
				uint32_t meshIdx = batch.meshIndex;
				uint32_t matIdx = batch.materialIndex;
				//if (batch.alphaMultiplier < 0.99f) continue;
				// 銆愬叧閿€戯細杩欓噷涓€瀹氳涔樹笂 lightVP锛岀畻鍑烘姇褰辩┖闂寸煩闃碉紒
				glm::mat4 lightModel = shadowSceneUniform.lightVP[i] * batch.transform;

				// 銆愬叧閿€戯細杩欓噷鍙帹閫?64 瀛楄妭 (sizeof(glm::mat4))锛佷笉瑕佷紶鏁翠釜 128 瀛楄妭锛?
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
					if (!batch.castShadow) {
						continue;
					}
					uint32_t meshIdx = batch.meshIndex;
					uint32_t matIdx = batch.materialIndex;

					auto jIt = aMeshJoints->find(meshIdx);
					auto wIt = aMeshWeights->find(meshIdx);
					if (jIt == aMeshJoints->end() || wIt == aMeshWeights->end()) continue;

					ShadowSkinnedPC pc{};
					pc.lightModel = shadowSceneUniform.lightVP[i] * batch.transform;
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
		shadowMapLayout = VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL;
	};

	auto portalReadyFor = [&](bool enabled,
		glsl::SceneUniform const* sceneUniform,
		ImageAndView const* color,
		ImageAndView const* bright,
		ImageAndView const* normal,
		ImageAndView const* depth,
		VkDescriptorSet surfaceDesc,
		glm::mat4 const* surfaceTransform) {
		return enabled &&
			sceneUniform != nullptr &&
			color != nullptr &&
			bright != nullptr &&
			normal != nullptr &&
			depth != nullptr &&
			aPortalSurfacePipe != VK_NULL_HANDLE &&
			surfaceDesc != VK_NULL_HANDLE &&
			surfaceTransform != nullptr &&
			aPortalMeshIndex < aMeshInfos.size();
	};

	const bool portalReady = portalReadyFor(
		aPortalEnabled,
		aPortalSceneUniform,
		aPortalColor,
		aPortalBright,
		aPortalNormal,
		aPortalDepth,
		aPortalSurfaceDesc,
		aPortalSurfaceTransform);
	const bool portal2Ready = portalReadyFor(
		aPortal2Enabled,
		aPortal2SceneUniform,
		aPortal2Color,
		aPortal2Bright,
		aPortal2Normal,
		aPortal2Depth,
		aPortal2SurfaceDesc,
		aPortal2SurfaceTransform);

	const bool portalScratchReady = portalReadyFor(
		aPortalEnabled,
		aPortalSceneUniform,
		aPortalRecursiveColor,
		aPortalRecursiveBright,
		aPortalRecursiveNormal,
		aPortalRecursiveDepth,
		aPortalRecursiveSurfaceDesc,
		aPortalSurfaceTransform);
	const bool portal2ScratchReady = portalReadyFor(
		aPortal2Enabled,
		aPortal2SceneUniform,
		aPortal2RecursiveColor,
		aPortal2RecursiveBright,
		aPortal2RecursiveNormal,
		aPortal2RecursiveDepth,
		aPortal2RecursiveSurfaceDesc,
		aPortal2SurfaceTransform);

	struct PortalTargetSet {
		ImageAndView const* color = nullptr;
		ImageAndView const* bright = nullptr;
		ImageAndView const* normal = nullptr;
		ImageAndView const* depth = nullptr;
		VkDescriptorSet surfaceDesc = VK_NULL_HANDLE;
	};

	struct NestedPortalSurface {
		bool draw = false;
		VkDescriptorSet surfaceDesc = VK_NULL_HANDLE;
		glm::mat4 const* surfaceTransform = nullptr;
		float phase = 0.0f;
	};

	auto renderPortalView = [&](glsl::SceneUniform const& portalSceneUniform,
		const std::vector<RenderBatch>* portalBatches,
		ImageAndView const& portalColor,
		ImageAndView const& portalBright,
		ImageAndView const& portalNormal,
		ImageAndView const& portalDepth,
		NestedPortalSurface const& nestedPortalA,
		NestedPortalSurface const& nestedPortalB) {
		uploadSceneUniform(portalSceneUniform);

		VkImageSubresourceRange portalColorRange{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
		VkImageSubresourceRange portalDepthRange{ VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1 };

		lut::image_barrier(aCmdBuff, portalColor.image,
			VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, VK_ACCESS_2_NONE, VK_IMAGE_LAYOUT_UNDEFINED,
			VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
			VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, portalColorRange);
		lut::image_barrier(aCmdBuff, portalBright.image,
			VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, VK_ACCESS_2_NONE, VK_IMAGE_LAYOUT_UNDEFINED,
			VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
			VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, portalColorRange);
		lut::image_barrier(aCmdBuff, portalNormal.image,
			VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, VK_ACCESS_2_NONE, VK_IMAGE_LAYOUT_UNDEFINED,
			VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
			VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, portalColorRange);
		lut::image_barrier(aCmdBuff, portalDepth.image,
			VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, VK_ACCESS_2_NONE, VK_IMAGE_LAYOUT_UNDEFINED,
			VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
			VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT,
			VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, portalDepthRange);

		VkRenderingAttachmentInfo portalDepthAtt{ VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
		portalDepthAtt.imageView = portalDepth.view;
		portalDepthAtt.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
		portalDepthAtt.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		portalDepthAtt.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		portalDepthAtt.clearValue.depthStencil = { 1.f, 0 };

		VkRenderingAttachmentInfo portalColorAtts[3]{};
		portalColorAtts[0].sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
		portalColorAtts[0].imageView = portalColor.view;
		portalColorAtts[0].imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		portalColorAtts[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		portalColorAtts[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		portalColorAtts[0].clearValue.color = { 0.02f, 0.03f, 0.05f, 1.0f };

		portalColorAtts[1] = portalColorAtts[0];
		portalColorAtts[1].imageView = portalBright.view;
		portalColorAtts[1].clearValue.color = { 0.f, 0.f, 0.f, 1.f };

		portalColorAtts[2] = portalColorAtts[0];
		portalColorAtts[2].imageView = portalNormal.view;
		portalColorAtts[2].clearValue.color = { 0.f, 0.f, 0.f, 0.f };

		VkRenderingInfo portalInfo{ VK_STRUCTURE_TYPE_RENDERING_INFO };
		portalInfo.renderArea.extent = aImageExtent;
		portalInfo.layerCount = 1;
		portalInfo.colorAttachmentCount = 3;
		portalInfo.pColorAttachments = portalColorAtts;
		portalInfo.pDepthAttachment = &portalDepthAtt;

		vkCmdBeginRendering(aCmdBuff, &portalInfo);

		VkViewport portalVp{};
		portalVp.x = 0.f;
		portalVp.y = 0.f;
		portalVp.width = float(aImageExtent.width);
		portalVp.height = float(aImageExtent.height);
		portalVp.minDepth = 0.f;
		portalVp.maxDepth = 1.f;
		vkCmdSetViewport(aCmdBuff, 0, 1, &portalVp);
		VkRect2D portalScissor{ {0, 0}, aImageExtent };
		vkCmdSetScissor(aCmdBuff, 0, 1, &portalScissor);

		vkCmdBindPipeline(aCmdBuff, VK_PIPELINE_BIND_POINT_GRAPHICS, aGraphicsPipe);
		vkCmdBindDescriptorSets(aCmdBuff, VK_PIPELINE_BIND_POINT_GRAPHICS, aGraphicsLayout, 0, 1, &aSceneDescriptors, 0, nullptr);

		VkDeviceSize portalZeroOffset = 0;
		const std::vector<RenderBatch>& batchesForPortal = portalBatches ? *portalBatches : aBatches;
		for (const auto& batch : batchesForPortal)
		{
			uint32_t matIdx = batch.materialIndex;
			uint32_t meshIdx = batch.meshIndex;
			if (meshIdx >= aMeshInfos.size() || matIdx >= aMaterialDescriptors.size()) {
				continue;
			}

			bool isMasked = (matIdx < aMaterials.size() && aMaterials[matIdx].alphaMaskTexture >= 0);
			if (!isMasked && batch.alphaMultiplier < 0.99f) {
				continue;
			}

			ObjectPC pcData{};
			pcData.transform = batch.transform;
			pcData.clipPlane = batch.clipPlane;
			if (matIdx < aMaterials.size()) {
				pcData.baseColorFactor = aMaterials[matIdx].baseColorFactor;
				pcData.emissiveFactor = aMaterials[matIdx].emissiveFactor;
				pcData.metallicFactor = aMaterials[matIdx].metallicFactor;
				pcData.roughnessFactor = aMaterials[matIdx].roughnessFactor;
				pcData.alphaCutoff = aMaterials[matIdx].alphaCutoff;
			}
			else {
				pcData.baseColorFactor = glm::vec4(1.0f);
				pcData.emissiveFactor = glm::vec4(0.0f);
				pcData.metallicFactor = 0.0f;
				pcData.roughnessFactor = 0.8f;
				pcData.alphaCutoff = 0.5f;
			}

			pcData.baseColorFactor.a *= batch.alphaMultiplier;

			vkCmdPushConstants(aCmdBuff, aGraphicsLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(ObjectPC), &pcData);
			vkCmdBindDescriptorSets(aCmdBuff, VK_PIPELINE_BIND_POINT_GRAPHICS, aGraphicsLayout, 1, 1, &aMaterialDescriptors[matIdx], 0, nullptr);
			vkCmdBindVertexBuffers(aCmdBuff, 0, 1, &aMeshPositions[meshIdx].buffer, &portalZeroOffset);
			vkCmdBindVertexBuffers(aCmdBuff, 1, 1, &aMeshTexCoords[meshIdx].buffer, &portalZeroOffset);
			vkCmdBindVertexBuffers(aCmdBuff, 2, 1, &aMeshNormals[meshIdx].buffer, &portalZeroOffset);
			vkCmdBindIndexBuffer(aCmdBuff, aMeshIndices[meshIdx].buffer, 0, VK_INDEX_TYPE_UINT32);
			vkCmdDrawIndexed(aCmdBuff, static_cast<uint32_t>(aMeshInfos[meshIdx].indices.size()), 1, 0, 0, 0);
		}

		if (aSkinnedPipe != VK_NULL_HANDLE && aSkinnedBatches && !aSkinnedBatches->empty() &&
			aMeshJoints && aMeshWeights && aBoneDescriptorSet != VK_NULL_HANDLE)
		{
			struct alignas(16) SkinnedPC {
				glm::mat4 transform;
				glm::vec4 baseColorFactor;
				glm::vec4 emissiveFactor;
				float metallicFactor;
				float roughnessFactor;
				float alphaCutoff;
				uint32_t boneBaseIndex;
				glm::vec4 clipPlane;
			};

			vkCmdBindPipeline(aCmdBuff, VK_PIPELINE_BIND_POINT_GRAPHICS, aSkinnedPipe);
			vkCmdBindDescriptorSets(aCmdBuff, VK_PIPELINE_BIND_POINT_GRAPHICS, aSkinnedPipeLayout, 0, 1, &aSceneDescriptors, 0, nullptr);
			vkCmdBindDescriptorSets(aCmdBuff, VK_PIPELINE_BIND_POINT_GRAPHICS, aSkinnedPipeLayout, 2, 1, &aBoneDescriptorSet, 0, nullptr);

			for (const auto& batch : *aSkinnedBatches) {
				uint32_t meshIdx = batch.meshIndex;
				uint32_t matIdx = batch.materialIndex;
				if (meshIdx >= aMeshInfos.size() || matIdx >= aMaterialDescriptors.size()) {
					continue;
				}

				const bool isAlpha =
					(matIdx < aMaterials.size() && aMaterials[matIdx].alphaMaskTexture >= 0) ||
					(batch.alphaMultiplier < 0.99f);
				if (isAlpha) {
					continue;
				}

				auto jIt = aMeshJoints->find(meshIdx);
				auto wIt = aMeshWeights->find(meshIdx);
				if (jIt == aMeshJoints->end() || wIt == aMeshWeights->end()) {
					continue;
				}

				SkinnedPC pc{};
				pc.transform = batch.transform;
				pc.boneBaseIndex = batch.boneBaseIndex;
				pc.clipPlane = batch.clipPlane;
				if (matIdx < aMaterials.size()) {
					pc.baseColorFactor = aMaterials[matIdx].baseColorFactor;
					pc.emissiveFactor = aMaterials[matIdx].emissiveFactor;
					pc.metallicFactor = aMaterials[matIdx].metallicFactor;
					pc.roughnessFactor = aMaterials[matIdx].roughnessFactor;
					pc.alphaCutoff = aMaterials[matIdx].alphaCutoff;
				}
				else {
					pc.baseColorFactor = glm::vec4(1.0f);
					pc.emissiveFactor = glm::vec4(0.0f);
					pc.metallicFactor = 0.0f;
					pc.roughnessFactor = 0.8f;
					pc.alphaCutoff = 0.5f;
				}

				vkCmdPushConstants(aCmdBuff, aSkinnedPipeLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(SkinnedPC), &pc);
				vkCmdBindDescriptorSets(aCmdBuff, VK_PIPELINE_BIND_POINT_GRAPHICS, aSkinnedPipeLayout, 1, 1, &aMaterialDescriptors[matIdx], 0, nullptr);

				VkDeviceSize z = 0;
				vkCmdBindVertexBuffers(aCmdBuff, 0, 1, &aMeshPositions[meshIdx].buffer, &z);
				vkCmdBindVertexBuffers(aCmdBuff, 1, 1, &aMeshTexCoords[meshIdx].buffer, &z);
				vkCmdBindVertexBuffers(aCmdBuff, 2, 1, &aMeshNormals[meshIdx].buffer, &z);
				vkCmdBindVertexBuffers(aCmdBuff, 3, 1, &jIt->second.buffer, &z);
				vkCmdBindVertexBuffers(aCmdBuff, 4, 1, &wIt->second.buffer, &z);
				vkCmdBindIndexBuffer(aCmdBuff, aMeshIndices[meshIdx].buffer, 0, VK_INDEX_TYPE_UINT32);
				vkCmdDrawIndexed(aCmdBuff, static_cast<uint32_t>(aMeshInfos[meshIdx].indices.size()), 1, 0, 0, 0);
			}
		}

		auto drawNestedPortalSurface = [&](NestedPortalSurface const& nestedPortal) {
			if (!nestedPortal.draw ||
				aPortalSurfacePipe == VK_NULL_HANDLE ||
				nestedPortal.surfaceDesc == VK_NULL_HANDLE ||
				nestedPortal.surfaceTransform == nullptr ||
				aPortalMeshIndex >= aMeshInfos.size())
			{
				return;
			}

			const uint32_t meshIdx = aPortalMeshIndex;
			PortalSurfacePC portalPc{};
			portalPc.transform = *nestedPortal.surfaceTransform;
			portalPc.portalParams = glm::vec4(aPortalEffectTime, nestedPortal.phase, 0.0f, 0.0f);
			vkCmdBindPipeline(aCmdBuff, VK_PIPELINE_BIND_POINT_GRAPHICS, aPortalSurfacePipe);
			vkCmdBindDescriptorSets(aCmdBuff, VK_PIPELINE_BIND_POINT_GRAPHICS, aGraphicsLayout, 0, 1, &aSceneDescriptors, 0, nullptr);
			vkCmdBindDescriptorSets(aCmdBuff, VK_PIPELINE_BIND_POINT_GRAPHICS, aGraphicsLayout, 1, 1, &nestedPortal.surfaceDesc, 0, nullptr);
			vkCmdPushConstants(aCmdBuff, aGraphicsLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PortalSurfacePC), &portalPc);
			vkCmdBindVertexBuffers(aCmdBuff, 0, 1, &aMeshPositions[meshIdx].buffer, &portalZeroOffset);
			vkCmdBindVertexBuffers(aCmdBuff, 1, 1, &aMeshTexCoords[meshIdx].buffer, &portalZeroOffset);
			vkCmdBindVertexBuffers(aCmdBuff, 2, 1, &aMeshNormals[meshIdx].buffer, &portalZeroOffset);
			vkCmdBindIndexBuffer(aCmdBuff, aMeshIndices[meshIdx].buffer, 0, VK_INDEX_TYPE_UINT32);
			vkCmdDrawIndexed(aCmdBuff, static_cast<uint32_t>(aMeshInfos[meshIdx].indices.size()), 1, 0, 0, 0);
		};
		drawNestedPortalSurface(nestedPortalA);
		drawNestedPortalSurface(nestedPortalB);

		if (skyboxPipe != VK_NULL_HANDLE && skyboxVBO != VK_NULL_HANDLE) {
			vkCmdBindPipeline(aCmdBuff, VK_PIPELINE_BIND_POINT_GRAPHICS, skyboxPipe);
			vkCmdSetViewport(aCmdBuff, 0, 1, &portalVp);
			vkCmdSetScissor(aCmdBuff, 0, 1, &portalScissor);
			vkCmdBindDescriptorSets(aCmdBuff, VK_PIPELINE_BIND_POINT_GRAPHICS, skyboxPipeLayout, 0, 1, &skyboxDescSet, 0, nullptr);
			VkDeviceSize offsets[1] = { 0 };
			vkCmdBindVertexBuffers(aCmdBuff, 0, 1, &skyboxVBO, offsets);
			vkCmdDraw(aCmdBuff, 36, 1, 0, 0);
		}

		vkCmdEndRendering(aCmdBuff);

		lut::image_barrier(aCmdBuff, portalColor.image,
			VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
			VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
			VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_READ_BIT,
			VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, portalColorRange);
	};

	const PortalTargetSet portalFinalTarget{
		aPortalColor,
		aPortalBright,
		aPortalNormal,
		aPortalDepth,
		aPortalSurfaceDesc
	};
	const PortalTargetSet portalScratchTarget{
		aPortalRecursiveColor,
		aPortalRecursiveBright,
		aPortalRecursiveNormal,
		aPortalRecursiveDepth,
		aPortalRecursiveSurfaceDesc
	};
	const PortalTargetSet portal2FinalTarget{
		aPortal2Color,
		aPortal2Bright,
		aPortal2Normal,
		aPortal2Depth,
		aPortal2SurfaceDesc
	};
	const PortalTargetSet portal2ScratchTarget{
		aPortal2RecursiveColor,
		aPortal2RecursiveBright,
		aPortal2RecursiveNormal,
		aPortal2RecursiveDepth,
		aPortal2RecursiveSurfaceDesc
	};

	auto portalLayerUniform = [&](uint32_t layer) -> glsl::SceneUniform const& {
		if (aPortalRecursiveSceneUniforms && layer < aPortalRecursiveSceneUniformCount) {
			return aPortalRecursiveSceneUniforms[layer];
		}
		return *aPortalSceneUniform;
	};

	auto portal2LayerUniform = [&](uint32_t layer) -> glsl::SceneUniform const& {
		if (aPortal2RecursiveSceneUniforms && layer < aPortal2RecursiveSceneUniformCount) {
			return aPortal2RecursiveSceneUniforms[layer];
		}
		return *aPortal2SceneUniform;
	};

	auto renderPortalOne = [&](PortalTargetSet const& target,
		glsl::SceneUniform const& sceneUniform,
		NestedPortalSurface const& nestedPortal1,
		NestedPortalSurface const& nestedPortal2) {
		const auto& portalShadowBatches = aPortalBatches ? *aPortalBatches : aBatches;
		renderShadowMap(sceneUniform, portalShadowBatches);
		renderPortalView(
			sceneUniform,
			aPortalBatches,
			*target.color,
			*target.bright,
			*target.normal,
			*target.depth,
			nestedPortal1,
			nestedPortal2);
	};

	auto renderPortalTwo = [&](PortalTargetSet const& target,
		glsl::SceneUniform const& sceneUniform,
		NestedPortalSurface const& nestedPortal1,
		NestedPortalSurface const& nestedPortal2) {
		const auto& portal2ShadowBatches = aPortal2Batches ? *aPortal2Batches : aBatches;
		renderShadowMap(sceneUniform, portal2ShadowBatches);
		renderPortalView(
			sceneUniform,
			aPortal2Batches,
			*target.color,
			*target.bright,
			*target.normal,
			*target.depth,
			nestedPortal1,
			nestedPortal2);
	};

	constexpr uint32_t kPortalRecursionLayers = 4;
	const uint32_t portalLayerCount = std::max(1u, std::min(kPortalRecursionLayers, aPortalRecursiveSceneUniformCount));
	const uint32_t portal2LayerCount = std::max(1u, std::min(kPortalRecursionLayers, aPortal2RecursiveSceneUniformCount));
	const uint32_t layerCount = std::max(portalLayerCount, portal2LayerCount);
	constexpr bool kDrawNestedPortalSurfaces = true;
	const bool portalRecursionRequested = kDrawNestedPortalSurfaces && layerCount > 1u && (
		(portalReady && (aPortalVisibleInPortalView || aPortal2VisibleInPortalView)) ||
		(portal2Ready && (aPortalVisibleInPortal2View || aPortal2VisibleInPortal2View)));
	const bool portalRecursionReady =
		(!portalReady || portalScratchReady) &&
		(!portal2Ready || portal2ScratchReady);

	if (portalRecursionRequested && portalRecursionReady) {
		const PortalTargetSet portalTargets[2] = { portalFinalTarget, portalScratchTarget };
		const PortalTargetSet portal2Targets[2] = { portal2FinalTarget, portal2ScratchTarget };
		const uint32_t deepestLayer = layerCount - 1u;

		uint32_t readIndex = 1;
		if (portalReady) {
			renderPortalOne(portalTargets[readIndex], portalLayerUniform(std::min(deepestLayer, portalLayerCount - 1u)), {}, {});
		}
		if (portal2Ready) {
			renderPortalTwo(portal2Targets[readIndex], portal2LayerUniform(std::min(deepestLayer, portal2LayerCount - 1u)), {}, {});
		}

		uint32_t writeIndex = 0;
		for (uint32_t step = 1; step < layerCount; ++step) {
			const uint32_t layer = deepestLayer - step;
			if (portalReady) {
				NestedPortalSurface nestedPortal1{
					aPortalVisibleInPortalView && portalReady,
					portalTargets[readIndex].surfaceDesc,
					aPortalSurfaceTransform,
					0.0f
				};
				NestedPortalSurface nestedPortal2{
					aPortal2VisibleInPortalView && portal2Ready,
					portal2Targets[readIndex].surfaceDesc,
					aPortal2SurfaceTransform,
					5.37f
				};
				renderPortalOne(portalTargets[writeIndex], portalLayerUniform(std::min(layer, portalLayerCount - 1u)), nestedPortal1, nestedPortal2);
			}
			if (portal2Ready) {
				NestedPortalSurface nestedPortal1{
					aPortalVisibleInPortal2View && portalReady,
					portalTargets[readIndex].surfaceDesc,
					aPortalSurfaceTransform,
					0.0f
				};
				NestedPortalSurface nestedPortal2{
					aPortal2VisibleInPortal2View && portal2Ready,
					portal2Targets[readIndex].surfaceDesc,
					aPortal2SurfaceTransform,
					5.37f
				};
				renderPortalTwo(portal2Targets[writeIndex], portal2LayerUniform(std::min(layer, portal2LayerCount - 1u)), nestedPortal1, nestedPortal2);
			}

			readIndex = writeIndex;
			writeIndex = 1 - writeIndex;
		}
	}
	else {
		if (portalReady) {
			renderPortalOne(portalFinalTarget, *aPortalSceneUniform, {}, {});
		}
		if (portal2Ready) {
			renderPortalTwo(portal2FinalTarget, *aPortal2SceneUniform, {}, {});
		}
	}
	renderShadowMap(aSceneUniform, aBatches);
	uploadSceneUniform(aSceneUniform);

	// ==========================================================
	// PASS 1: 鍦烘櫙娓叉煋 + 浜害鎻愬彇 (MRT -> Offscreen + Bright)
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

	// 銆愭柊澧炶ˉ婕?1銆戯細灏嗘硶绾跨紦鍐查噸缃负鍙啓鍏ョ姸鎬?
	lut::image_barrier(aCmdBuff, aNormalImage.image,
		VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, VK_ACCESS_2_NONE, VK_IMAGE_LAYOUT_UNDEFINED,
		VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
		VkImageSubresourceRange{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 }
	);

	// 銆愭柊澧炶ˉ婕?2銆戯細灏嗘繁搴︾紦鍐查噸缃负鍙鍐欑姸鎬侊紒(鏋佸叾鍏抽敭锛屽惁鍒欐繁搴︽祴璇曞叏宕?
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

	// 闄勪欢 0: Scene Color
	colorAtts[0].sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
	colorAtts[0].imageView = aOffscreenColor.view;
	colorAtts[0].imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	colorAtts[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	colorAtts[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	colorAtts[0].clearValue.color = aClearColor;

	// 闄勪欢 1: Bright Color
	colorAtts[1] = colorAtts[0];
	colorAtts[1].imageView = aBrightColor.view;
	colorAtts[1].clearValue.color = { 0.f, 0.f, 0.f, 1.f };

	// 銆愭柊澧炪€戦檮浠?2: Normal Buffer
	colorAtts[2] = colorAtts[0];
	colorAtts[2].imageView = aNormalImage.view;
	colorAtts[2].clearValue.color = { 0.f, 0.f, 0.f, 0.f }; // 榛樿鑳屾櫙鏃犳硶绾夸笖缁濆鍏夋粦(0)

	VkRenderingInfo mrtInfo{ VK_STRUCTURE_TYPE_RENDERING_INFO };
	mrtInfo.renderArea.extent = aImageExtent;
	mrtInfo.layerCount = 1;
	mrtInfo.colorAttachmentCount = 3; // <-- 鏀逛负 3
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
	// 闃舵 1锛氬厛鐢汇€愬疄蹇冦€戠墿浣?(鍖呮嫭鏍戝彾 Mask)
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

		// --- 瀹屽叏銆佸共鍑€鍦板垵濮嬪寲 ObjectPC ---
		ObjectPC pcData{};
		pcData.transform = batch.transform;
		pcData.clipPlane = batch.clipPlane;

		if (matIdx < aMaterials.size()) {
			pcData.baseColorFactor = aMaterials[matIdx].baseColorFactor;
			pcData.emissiveFactor = aMaterials[matIdx].emissiveFactor;
			pcData.metallicFactor = aMaterials[matIdx].metallicFactor;
			pcData.roughnessFactor = aMaterials[matIdx].roughnessFactor;
			pcData.alphaCutoff = aMaterials[matIdx].alphaCutoff;
		}
		else {
			// 瀹夊叏鍥為€€锛氶粯璁ら潪閲戝睘銆佺矖绯欍€佹棤鑷彂鍏?
			pcData.baseColorFactor = glm::vec4(1.0f);
			pcData.emissiveFactor = glm::vec4(0.0f);
			pcData.metallicFactor = 0.0f;
			pcData.roughnessFactor = 0.8f;
			pcData.alphaCutoff = 0.5f;
		}

		pcData.baseColorFactor.a *= batch.alphaMultiplier;

		// 鍘嬪叆绠＄嚎
		vkCmdPushConstants(aCmdBuff, aGraphicsLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(ObjectPC), &pcData);

		vkCmdBindDescriptorSets(aCmdBuff, VK_PIPELINE_BIND_POINT_GRAPHICS, aGraphicsLayout, 1, 1, &aMaterialDescriptors[matIdx], 0, nullptr);
		vkCmdBindVertexBuffers(aCmdBuff, 0, 1, &aMeshPositions[meshIdx].buffer, &kZeroOffset);
		vkCmdBindVertexBuffers(aCmdBuff, 1, 1, &aMeshTexCoords[meshIdx].buffer, &kZeroOffset);
		vkCmdBindVertexBuffers(aCmdBuff, 2, 1, &aMeshNormals[meshIdx].buffer, &kZeroOffset);

		vkCmdBindIndexBuffer(aCmdBuff, aMeshIndices[meshIdx].buffer, 0, VK_INDEX_TYPE_UINT32);
		vkCmdDrawIndexed(aCmdBuff, static_cast<uint32_t>(meshInfo.indices.size()), 1, 0, 0, 0);
	}

	if (portalReady && aPortalMainVisible)
	{
		uint32_t meshIdx = aPortalMeshIndex;
		PortalSurfacePC portalPc{};
		portalPc.transform = *aPortalSurfaceTransform;
		portalPc.portalParams = glm::vec4(aPortalEffectTime, 0.0f, 0.0f, 0.0f);
		vkCmdBindPipeline(aCmdBuff, VK_PIPELINE_BIND_POINT_GRAPHICS, aPortalSurfacePipe);
		vkCmdBindDescriptorSets(aCmdBuff, VK_PIPELINE_BIND_POINT_GRAPHICS, aGraphicsLayout, 0, 1, &aSceneDescriptors, 0, nullptr);
		vkCmdBindDescriptorSets(aCmdBuff, VK_PIPELINE_BIND_POINT_GRAPHICS, aGraphicsLayout, 1, 1, &aPortalSurfaceDesc, 0, nullptr);
		vkCmdPushConstants(aCmdBuff, aGraphicsLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PortalSurfacePC), &portalPc);
		vkCmdBindVertexBuffers(aCmdBuff, 0, 1, &aMeshPositions[meshIdx].buffer, &kZeroOffset);
		vkCmdBindVertexBuffers(aCmdBuff, 1, 1, &aMeshTexCoords[meshIdx].buffer, &kZeroOffset);
		vkCmdBindVertexBuffers(aCmdBuff, 2, 1, &aMeshNormals[meshIdx].buffer, &kZeroOffset);
		vkCmdBindIndexBuffer(aCmdBuff, aMeshIndices[meshIdx].buffer, 0, VK_INDEX_TYPE_UINT32);
		vkCmdDrawIndexed(aCmdBuff, static_cast<uint32_t>(aMeshInfos[meshIdx].indices.size()), 1, 0, 0, 0);
	}
	if (portal2Ready && aPortal2MainVisible)
	{
		uint32_t meshIdx = aPortalMeshIndex;
		PortalSurfacePC portalPc{};
		portalPc.transform = *aPortal2SurfaceTransform;
		portalPc.portalParams = glm::vec4(aPortalEffectTime, 5.37f, 0.0f, 0.0f);
		vkCmdBindPipeline(aCmdBuff, VK_PIPELINE_BIND_POINT_GRAPHICS, aPortalSurfacePipe);
		vkCmdBindDescriptorSets(aCmdBuff, VK_PIPELINE_BIND_POINT_GRAPHICS, aGraphicsLayout, 0, 1, &aSceneDescriptors, 0, nullptr);
		vkCmdBindDescriptorSets(aCmdBuff, VK_PIPELINE_BIND_POINT_GRAPHICS, aGraphicsLayout, 1, 1, &aPortal2SurfaceDesc, 0, nullptr);
		vkCmdPushConstants(aCmdBuff, aGraphicsLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PortalSurfacePC), &portalPc);
		vkCmdBindVertexBuffers(aCmdBuff, 0, 1, &aMeshPositions[meshIdx].buffer, &kZeroOffset);
		vkCmdBindVertexBuffers(aCmdBuff, 1, 1, &aMeshTexCoords[meshIdx].buffer, &kZeroOffset);
		vkCmdBindVertexBuffers(aCmdBuff, 2, 1, &aMeshNormals[meshIdx].buffer, &kZeroOffset);
		vkCmdBindIndexBuffer(aCmdBuff, aMeshIndices[meshIdx].buffer, 0, VK_INDEX_TYPE_UINT32);
		vkCmdDrawIndexed(aCmdBuff, static_cast<uint32_t>(aMeshInfos[meshIdx].indices.size()), 1, 0, 0, 0);
	}

	// =====================================================================
	// 闃舵 1.5锛氱敾銆愬ぉ绌虹洅銆?(姝ゆ椂鍒╃敤 Early-Z 鍓旈櫎琚缓绛戞尅浣忕殑澶╃┖鍍忕礌锛屾€ц兘鏋侀珮锛?
	// =====================================================================
	if (skyboxPipe != VK_NULL_HANDLE && skyboxVBO != VK_NULL_HANDLE) {
		vkCmdBindPipeline(aCmdBuff, VK_PIPELINE_BIND_POINT_GRAPHICS, skyboxPipe);

		// 鏄惧紡璁剧疆瑙嗗彛鍜岃鍓尯鍩?
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

		// 缁戝畾澶╃┖鐩掔殑绠＄嚎甯冨眬鍜屾弿杩扮锛屽崰鐢?Set 0
		vkCmdBindDescriptorSets(aCmdBuff, VK_PIPELINE_BIND_POINT_GRAPHICS, skyboxPipeLayout, 0, 1, &skyboxDescSet, 0, nullptr);
		VkDeviceSize offsets[1] = { 0 };
		vkCmdBindVertexBuffers(aCmdBuff, 0, 1, &skyboxVBO, offsets);

		vkCmdDraw(aCmdBuff, 36, 1, 0, 0);
	}

	// =====================================================================
	// 銆愭柊澧炪€戯細鍦?MRT 缁撴潫鍓嶏紝琛ヤ笂涓嶉€忔槑鐨勯楠煎姩鐢伙紒
	// 杩欐牱瀹冧滑鎵嶈兘鎶婃繁搴﹀拰娉曠嚎鍐欏叆 G-Buffer锛屽弬涓?SSR 鍜?SSAO
	// =====================================================================
	if (aSkinnedPipe != VK_NULL_HANDLE && aSkinnedBatches && !aSkinnedBatches->empty() && aMeshJoints && aMeshWeights && aBoneDescriptorSet != VK_NULL_HANDLE)
	{
		struct alignas(16) SkinnedPC {
			glm::mat4 transform;        // offset 0
			glm::vec4 baseColorFactor;  // offset 64
			glm::vec4 emissiveFactor;   // offset 80 (matches default.frag)
			float metallicFactor;       // offset 96
			float roughnessFactor;      // offset 100
			float alphaCutoff;          // offset 104
			uint32_t boneBaseIndex;     // offset 108
			glm::vec4 clipPlane;        // offset 112
		};
		vkCmdBindPipeline(aCmdBuff, VK_PIPELINE_BIND_POINT_GRAPHICS, aSkinnedPipe);
		vkCmdBindDescriptorSets(aCmdBuff, VK_PIPELINE_BIND_POINT_GRAPHICS, aSkinnedPipeLayout, 0, 1, &aSceneDescriptors, 0, nullptr);
		vkCmdBindDescriptorSets(aCmdBuff, VK_PIPELINE_BIND_POINT_GRAPHICS, aSkinnedPipeLayout, 2, 1, &aBoneDescriptorSet, 0, nullptr);

		for (const auto& batch : *aSkinnedBatches) {
			uint32_t meshIdx = batch.meshIndex; uint32_t matIdx = batch.materialIndex;
			// 鍙敾涓嶉€忔槑鐨?
			bool isAlpha = (matIdx < aMaterials.size() && aMaterials[matIdx].alphaMaskTexture >= 0) || (batch.alphaMultiplier < 0.99f);
			if (isAlpha) continue;

			auto jIt = aMeshJoints->find(meshIdx); auto wIt = aMeshWeights->find(meshIdx);
			if (jIt == aMeshJoints->end() || wIt == aMeshWeights->end()) continue;

			SkinnedPC pc{};
			pc.transform = batch.transform; pc.boneBaseIndex = batch.boneBaseIndex;
			pc.clipPlane = batch.clipPlane;
			if (matIdx < aMaterials.size()) {
				pc.baseColorFactor = aMaterials[matIdx].baseColorFactor;
				pc.emissiveFactor = aMaterials[matIdx].emissiveFactor;
				pc.metallicFactor = aMaterials[matIdx].metallicFactor;
				pc.roughnessFactor = aMaterials[matIdx].roughnessFactor;
				pc.alphaCutoff = aMaterials[matIdx].alphaCutoff;
			}
			else {
				pc.baseColorFactor = glm::vec4(1.0f); pc.emissiveFactor = glm::vec4(0.0f); pc.metallicFactor = 0.0f; pc.roughnessFactor = 0.8f; pc.alphaCutoff = 0.5f;
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

	vkCmdEndRendering(aCmdBuff); // <---- 鐪熸鐨?MRT 闃舵缁撴潫锛?

	// ==========================================================
	// 鍑嗗杩涘叆 SSR 闃舵锛氬皢鍒氬垰鐢诲ソ鐨勬硶绾裤€佹繁搴︺€侀鑹茶浆涓哄彧璇婚噰鏍风姸鎬?
	// ==========================================================
	VkImageSubresourceRange range{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
	VkImageSubresourceRange depthRange{ VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1 };

	// 1. 娉曠嚎 -> Read
	lut::image_barrier(aCmdBuff, aNormalImage.image,
		VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
		VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_READ_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, range);

	// 2. 鍦烘櫙棰滆壊 -> Read (鍥犱负 SSR 瑕侀噰鍘熸湰鐨勭敾闈㈤鑹插綋鍊掑奖)
	lut::image_barrier(aCmdBuff, aOffscreenColor.image,
		VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
		VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_READ_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, range);

	// 3. 娣卞害鍥?-> Read (鍥犱负 SSR 瑕侀潬娣卞害閲嶅缓 3D 鍧愭爣)
	lut::image_barrier(aCmdBuff, aDepthAttach.image,
		VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT, VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
		VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_READ_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, depthRange);

	// 4. SSR Output -> Write (浣滀负 SSR Pass 鐨勭敾鏉?
	lut::image_barrier(aCmdBuff, aSsrOutput.image,
		VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, VK_ACCESS_2_NONE, VK_IMAGE_LAYOUT_UNDEFINED,
		VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, range);
	// ==========================================================
	// 鎵ц SSR Pass
	// ==========================================================
	VkRenderingAttachmentInfo ssrAtt{ VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
	ssrAtt.imageView = aSsrOutput.view;
	ssrAtt.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	// 椤烘墜鎶婅繖閲屾敼鎴?CLEAR锛屼互闃蹭竾涓€娈嬬暀鍨冨溇鏁版嵁
	ssrAtt.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	ssrAtt.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	//ssrAtt.clearValue.color = { 0.0f, 0.0f, 0.0f, 0.0f }; // 娓呯┖涓虹函榛?

	VkRenderingInfo ssrRenderInfo{ VK_STRUCTURE_TYPE_RENDERING_INFO };
	ssrRenderInfo.renderArea.extent = aImageExtent;
	ssrRenderInfo.layerCount = 1;
	ssrRenderInfo.colorAttachmentCount = 1;
	ssrRenderInfo.pColorAttachments = &ssrAtt;

	vkCmdBeginRendering(aCmdBuff, &ssrRenderInfo);
	// 銆愭牳蹇冧慨鏀广€戯細鍙湁寮€鍚簡 SSR锛屾墠缁?GPU 涓嬪彂缁樺埗鎸囦护锛?
	if (aSsrEnabled) {
		vkCmdBindPipeline(aCmdBuff, VK_PIPELINE_BIND_POINT_GRAPHICS, aSsrPipe);
		vkCmdBindDescriptorSets(aCmdBuff, VK_PIPELINE_BIND_POINT_GRAPHICS, aSsrLayout, 0, 1, &aSsrDS, 0, nullptr);

		//float ssrParams[4] = { 0.2f /*姝ラ暱*/, 100.0f /*鏈€澶ф鏁?/, 0.1f /*鍘氬害*/, 0.0f /*鐣欑櫧*/ };
		// 缂╁皬姝ラ暱璁╁皠绾挎洿瀵嗭紝澧炲姞鍘氬害闃叉灏勭嚎鈥滅┛妯♀€濇紡鍒?

		// 鍦?rendering.cpp 涓慨鏀?ssrParams锛?
		float ssrParams[4] = {
			0.05f,  /* 姝ラ暱锛氱◢寰皟瀵嗕竴鐐癸紝閰嶅悎浜屽垎鏌ユ壘绮惧害鏋侀珮 */
			128.0f, /* 鏈€澶ф鏁?*/
			0.3f,   /* 馃敶 鍘氬害锛?.12 澶杽浼氬鑷村ぇ闈㈢Н婕忓垽锛?.5 浼氭嫋褰憋紝0.3 鏄渶瀹岀編鐨勯粍閲戠偣 */
			100.0f  /* 鏈€澶у弽灏勮窛绂?*/
		};
		vkCmdPushConstants(aCmdBuff, aSsrLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(float) * 4, ssrParams);
		vkCmdSetViewport(aCmdBuff, 0, 1, &vp);
		vkCmdSetScissor(aCmdBuff, 0, 1, &scissor);

		vkCmdDraw(aCmdBuff, 3, 1, 0, 0); // 瑙﹀彂鍏ㄥ睆鍥涜竟褰?
	}
	vkCmdEndRendering(aCmdBuff);
	// SSR 杈撳嚭瀹屾瘯锛岃浆涓哄彧璇伙紝鐣欑粰 Composite 闃舵鍘绘贩鍚堬紒
	lut::image_barrier(aCmdBuff, aSsrOutput.image,
		VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
		VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_READ_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, range);

	// ==========================================================
	// SSAO Pass (娴佺▼鍚?SSR锛屽厛杞姸鎬併€佸啀娓叉煋銆佹渶鍚庤浆鍥炲彧璇?
	// ==========================================================
	// ==========================================================
	// 鎵ц SSAO Pass
	// ==========================================================

	// 銆愪慨姝ｃ€戯細鍙妸 SSAO 鐨勭敾鏉胯浆涓哄彲鍐欑姸鎬侊紒涓嶈纰?SSR锛?
	lut::image_barrier(aCmdBuff, aSsaoRawOutput.image,
		VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, VK_ACCESS_2_NONE, VK_IMAGE_LAYOUT_UNDEFINED,
		VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, range);

	VkRenderingAttachmentInfo ssaoAtt{ VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
	ssaoAtt.imageView = aSsaoRawOutput.view;
	ssaoAtt.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	ssaoAtt.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	ssaoAtt.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	// 榛樿娓呯┖涓虹函鐧?(1.0)锛岃〃绀烘病鏈夐伄钄?
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

		// 鎺ㄩ€?SSAO 鍙傛暟锛歔閲囨牱鍗婂緞, 鍋忕Щ瀹瑰樊, 闃村奖寮哄害, 鍗犱綅绗
		float ssaoParams[4] = { 0.5f, 0.025f, 1.5f, 0.0f };
		vkCmdPushConstants(aCmdBuff, aSsaoLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(float) * 4, ssaoParams);

		vkCmdSetViewport(aCmdBuff, 0, 1, &vp);
		vkCmdSetScissor(aCmdBuff, 0, 1, &scissor);

		vkCmdDraw(aCmdBuff, 3, 1, 0, 0);
	}
	vkCmdEndRendering(aCmdBuff);

	// SSAO 杈撳嚭瀹屾瘯锛岃浆涓哄彧璇伙紝鍑嗗缁欎笅涓樁娈碉紙Blur 鎴栬€?Composite锛変娇鐢?
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
	// 涓?Composite 鐢绘澘鍑嗗灞忛殰 (鏋佸叾閲嶈锛岄槻 Vulkan 鎶ラ敊)
	// ==========================================================
	//VkImageSubresourceRange range{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
	lut::image_barrier(aCmdBuff, aCompositeOutput.image,
		VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, VK_ACCESS_2_NONE, VK_IMAGE_LAYOUT_UNDEFINED,
		VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, range);

	// ==========================================================
	// 1. Composite Pass (缁堟瀬鍚堟垚闃舵: 鍦烘櫙搴曡壊 + Bloom + 鎵€鏈夊崐閫忔槑鐗╀綋)
	// ==========================================================

	// 銆愭牳蹇冧慨澶嶃€戯細灏嗘繁搴﹀浘杞负鈥滃彧璇绘繁搴︽祴璇曗€濈姸鎬侊紒
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
	compDepthAtt.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;   // 淇濈暀涓嶉€忔槑鐗╀綋鐨勬繁搴︼紒
	compDepthAtt.storeOp = VK_ATTACHMENT_STORE_OP_NONE; // 鍗婇€忔槑涓嶅啓娣卞害

	VkRenderingAttachmentInfo compAtt{};
	compAtt.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
	compAtt.imageView = aCompositeOutput.view; // 闃舵 1 鐢诲埌涓浆缂撳啿
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
	compInfo.pDepthAttachment = &compDepthAtt; // 鎸傝浇娣卞害鐢绘澘锛?

	vkCmdBeginRendering(aCmdBuff, &compInfo);

	// --- 1. 鐢诲叏灞忚儗鏅悎鎴?(SSAO + SSR + 鍦烘櫙搴曡壊) ---
	vkCmdBindPipeline(aCmdBuff, VK_PIPELINE_BIND_POINT_GRAPHICS, aCompositePipe);
	vkCmdBindDescriptorSets(aCmdBuff, VK_PIPELINE_BIND_POINT_GRAPHICS, aCompositeLayout, 0, 1, &aCompositeDS, 0, nullptr);
	struct BloomPC { float exposure; float strength; float _pad[2]; } bloomPC{ 1.0f, aBloomStrength, {0.0f,0.0f} };
	vkCmdPushConstants(aCmdBuff, aCompositeLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(BloomPC), &bloomPC);
	vkCmdSetViewport(aCmdBuff, 0, 1, &vp);
	vkCmdSetScissor(aCmdBuff, 0, 1, &scissor);
	vkCmdDraw(aCmdBuff, 3, 1, 0, 0);

	// --- 2. 鐢诲崐閫忔槑闈欐€佺墿浣?---
	vkCmdBindPipeline(aCmdBuff, VK_PIPELINE_BIND_POINT_GRAPHICS, aAlphaPipe);
	vkCmdBindDescriptorSets(aCmdBuff, VK_PIPELINE_BIND_POINT_GRAPHICS, aGraphicsLayout, 0, 1, &aSceneDescriptors, 0, nullptr);

	 kZeroOffset = 0; // 銆愪慨澶?1銆戯細琛ヤ笂绫诲瀷澹版槑
	for (const auto& batch : aBatches) {
		uint32_t meshIdx = batch.meshIndex; uint32_t matIdx = batch.materialIndex;
		bool isMatMasked = (matIdx < aMaterials.size() && aMaterials[matIdx].alphaMaskTexture >= 0);
		if (isMatMasked) continue;
		if (batch.alphaMultiplier >= 0.99f) continue;

		auto const& meshInfo = aMeshInfos[meshIdx];
		ObjectPC pcData{};
		pcData.transform = batch.transform;
		pcData.clipPlane = batch.clipPlane;
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

	// --- 3. 鐢诲崐閫忔槑楠ㄩ鍔ㄧ敾 ---
	if (aSkinnedAlphaPipe != VK_NULL_HANDLE && aSkinnedBatches && !aSkinnedBatches->empty() && aBoneDescriptorSet != VK_NULL_HANDLE) {
		struct alignas(16) SkinnedPC {
			glm::mat4 transform;        // offset 0
			glm::vec4 baseColorFactor;  // offset 64
			glm::vec4 emissiveFactor;   // offset 80 (matches default.frag)
			float metallicFactor;       // offset 96
			float roughnessFactor;      // offset 100
			float alphaCutoff;          // offset 104
			uint32_t boneBaseIndex;     // offset 108
			glm::vec4 clipPlane;        // offset 112
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
			pc.clipPlane = batch.clipPlane;
			if (matIdx < aMaterials.size()) {
				pc.baseColorFactor = aMaterials[matIdx].baseColorFactor;
				pc.emissiveFactor = aMaterials[matIdx].emissiveFactor;
				pc.metallicFactor = aMaterials[matIdx].metallicFactor;
				pc.roughnessFactor = aMaterials[matIdx].roughnessFactor;
				pc.alphaCutoff = aMaterials[matIdx].alphaCutoff;
			}
			else {
				pc.baseColorFactor = glm::vec4(1.0f); pc.emissiveFactor = glm::vec4(0.0f); pc.metallicFactor = 0.0f; pc.roughnessFactor = 0.8f; pc.alphaCutoff = 0.5f;
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

	// --- 4. 鐢荤矑瀛愮郴缁?---
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

	// --- 5. 鐢昏皟璇曠嚎 ---
	aDebugRenderer.Render(aCmdBuff, aDebugLinePipe, aGraphicsLayout, aSceneDescriptors);

	// 鏈€缁堝悎鎴愭覆鏌撻€氶亾缁撴潫锛?
	vkCmdEndRendering(aCmdBuff);

	// ==========================================================
	// 2. 銆愯В寮€娉ㄩ噴骞朵慨澶嶃€戯細Speed Post-Process Pass
	// ==========================================================
	// 灏?aCompositeOutput (涓婁竴灞傜殑鎴愬搧) 杞崲涓哄彧璇伙紝鍑嗗浣滀负绾圭悊琚噰鏍?
	lut::image_barrier(aCmdBuff, aCompositeOutput.image,
		VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
		VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_READ_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, range);

	// 涓?aFinalSceneColor (缁堟瀬杞戒綋) 鍑嗗鍐欏叆鐘舵€?
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
	// 1. 缁勮涓€涓寘鍚墍鏈夊悗澶勭悊鍙傛暟鐨勭粨鏋勪綋 (娉ㄦ剰 16 瀛楄妭瀵归綈)
	struct alignas(16) PostProcessPC {
		float speedFactor; // 鏋侀€熺壒鏁堟瘮渚?
		float deathFactor; // 姝讳骸婊ら暅姣斾緥 (0.0 = 娲? 1.0 = 姝?
		float _pad[2];     // 琛ラ綈鍒?16 瀛楄妭锛岄槻姝?Vulkan 鎶ラ敊
	};

	PostProcessPC ppData{};
	ppData.speedFactor = aSpeedFactor;
	ppData.deathFactor = deathFactor; // <--- 杩欓噷鐩存帴浼犲叆 0.0 鍒?1.0 涔嬮棿鐨勬彃鍊?

	// 2. 灏嗘墦鍖呭ソ鐨勭粨鏋勪綋鎺ㄩ€佺粰 Shader
	vkCmdPushConstants(aCmdBuff, aSpeedLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PostProcessPC), &ppData);
	vkCmdSetViewport(aCmdBuff, 0, 1, &vp);
	vkCmdSetScissor(aCmdBuff, 0, 1, &scissor);
	vkCmdDraw(aCmdBuff, 3, 1, 0, 0);
	vkCmdEndRendering(aCmdBuff);

	// ==========================================================
	// 3. UI Render Pass: Render everything onto the actual Swapchain
	// ==========================================================

	// 杞崲 aFinalSceneColor 鐨勭姸鎬侊紝鍥犱负瀹冪幇鍦ㄦ槸 3D 鍦烘櫙鐨勮浇浣擄紝ImGui 闇€瑕佹妸瀹冨綋璐村浘璇?
	lut::image_barrier(aCmdBuff, aFinalSceneColor.image,
		VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
		VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_READ_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, range);

	// 鍑嗗鐗╃悊灞忓箷 (Swapchain) 鎺ユ敹 UI 娓叉煋
	lut::image_barrier(aCmdBuff, aSwapchainAttach.image,
		VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, VK_ACCESS_2_NONE, VK_IMAGE_LAYOUT_UNDEFINED,
		VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, range);

	VkRenderingAttachmentInfo uiColorAttach{ VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
	uiColorAttach.imageView = aSwapchainAttach.view; // 鐪熸缁樺埗鍒板睆骞?
	uiColorAttach.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	uiColorAttach.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	uiColorAttach.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	uiColorAttach.clearValue.color = { 0.12f, 0.12f, 0.12f, 1.0f }; // UI 鑳屾櫙鏉跨殑搴曡壊锛屽彲鏀?

	VkRenderingInfo uiRenderInfo{ VK_STRUCTURE_TYPE_RENDERING_INFO };
	uiRenderInfo.renderArea.offset = { 0, 0 };
	uiRenderInfo.renderArea.extent = aImageExtent;
	uiRenderInfo.layerCount = 1;
	uiRenderInfo.colorAttachmentCount = 1;
	uiRenderInfo.pColorAttachments = &uiColorAttach;

	vkCmdBeginRendering(aCmdBuff, &uiRenderInfo);

	// 鐪熸瑙﹀彂 ImGui 鐨勬覆鏌擄紒
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
