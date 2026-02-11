#pragma once

#include <volk/volk.h>
#include <vector>

#include "setup.hpp"
#include "camera.hpp"
#include "baked_model.hpp"
#include "../labut2/vkobject.hpp"
#include "../labut2/vulkan_window.hpp"
#include "../labut2/vkbuffer.hpp" // Added

namespace lut = labut2;



void record_commands( 
	VkCommandBuffer aCmdBuff, 
	VkPipeline aGraphicsPipe, 
	VkPipeline aAlphaPipe, 
	ImageAndView const& aColorAttach, 
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
	std::vector<BakedMeshData> const& aMeshInfos, 
	std::vector<BakedMaterialInfo> const& aMaterials,
	std::vector<VkDescriptorSet> const& aMaterialDescriptors,
	VkPipeline aPostProcPipe,
	VkDescriptorSet aPostProcDescriptors,
	VkPipelineLayout aPostProcLayout,
	ImageAndView const& aOffscreenColor,
	VkClearColorValue aClearColor,
	// p2_1.5 shadow mapping
	VkPipeline aShadowPipe,
	ImageAndView const& aShadowMap
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
