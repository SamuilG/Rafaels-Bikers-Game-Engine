#include "setup.hpp"

#include "../labut2/error.hpp"
#include "../labut2/to_string.hpp"
#include "../labut2/load.hpp"


#include <glm/glm.hpp>
#include <glm/gtx/transform.hpp>
#include <glm/gtc/matrix_transform.hpp>


lut::PipelineLayout create_triangle_pipeline_layout( lut::VulkanContext const& aContext, VkDescriptorSetLayout aSceneLayout, VkDescriptorSetLayout aObjectLayout )
{
	VkDescriptorSetLayout layouts[] = {
		// Order must match the set = N in the shaders
		aSceneLayout, // set 0
		aObjectLayout // set 1
	};

	//matrix calculate
	VkPushConstantRange pushConstant{};
	pushConstant.stageFlags = VK_SHADER_STAGE_VERTEX_BIT; 
	pushConstant.offset = 0;
	pushConstant.size = sizeof(glm::mat4); 


	VkPipelineLayoutCreateInfo layoutInfo{};
	layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	layoutInfo.setLayoutCount = sizeof(layouts)/sizeof(layouts[0]); 
	layoutInfo.pSetLayouts = layouts; 
	layoutInfo.pushConstantRangeCount = 1;
	layoutInfo.pPushConstantRanges = &pushConstant;

	VkPipelineLayout layout = VK_NULL_HANDLE;
	if( auto const res = vkCreatePipelineLayout( aContext.device, &layoutInfo, nullptr, &layout ); VK_SUCCESS != res )
	{
		throw lut::Error( "Unable to create pipeline layout\n"
			"vkCreatePipelineLayout() returned {}", lut::to_string(res)
		);
	}

	return lut::PipelineLayout( aContext.device, layout );
}

lut::PipelineLayout create_post_proc_pipeline_layout( lut::VulkanContext const& aContext, VkDescriptorSetLayout aDescriptorLayout )
{
	VkDescriptorSetLayout layouts[] = {
		aDescriptorLayout // set 0
	};

	VkPipelineLayoutCreateInfo layoutInfo{};
	layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	layoutInfo.setLayoutCount = 1;
	layoutInfo.pSetLayouts = layouts;
	layoutInfo.pushConstantRangeCount = 0;
	layoutInfo.pPushConstantRanges = nullptr;

	VkPipelineLayout layout = VK_NULL_HANDLE;
	if( auto const res = vkCreatePipelineLayout( aContext.device, &layoutInfo, nullptr, &layout ); VK_SUCCESS != res )
	{
		throw lut::Error( "Unable to create pipeline layout\n"
			"vkCreatePipelineLayout() returned {}", lut::to_string(res)
		);
	}

	return lut::PipelineLayout( aContext.device, layout );
}


lut::Pipeline create_triangle_pipeline( lut::VulkanWindow const& aWindow, VkPipelineLayout aPipelineLayout, VkFormat aColorFormat )
{
	// Load shader code
	auto const vertSpirV = lut::load_file_u32( cfg::kVertShaderPath );
	auto const fragSpirV = lut::load_file_u32( cfg::kFragShaderPath );

	VkShaderModuleCreateInfo code[2]{};
	code[0].sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	code[0].codeSize = vertSpirV.size()*sizeof(std::uint32_t);
	code[0].pCode = vertSpirV.data();

	code[1].sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	code[1].codeSize = fragSpirV.size()*sizeof(std::uint32_t);
	code[1].pCode = fragSpirV.data();

	// Define shader stages in the pipeline
	VkPipelineShaderStageCreateInfo stages[2]{};
	stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
	stages[0].pName = "main";
	stages[0].pNext = &code[0];

	stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
	stages[1].pName = "main";
	stages[1].pNext = &code[1];

	VkVertexInputBindingDescription vertexInputs[3]{};
	vertexInputs[0].binding = 0;
	vertexInputs[0].stride = sizeof(float)*3; 
	vertexInputs[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

	vertexInputs[1].binding = 1;
	vertexInputs[1].stride = sizeof(float)*2; 
	vertexInputs[1].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

	vertexInputs[2].binding = 2;
	vertexInputs[2].stride = sizeof(float)*3; 
	vertexInputs[2].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

	VkVertexInputAttributeDescription vertexAttributes[3]{};
	vertexAttributes[0].binding = 0; 
	vertexAttributes[0].location = 0; 
	vertexAttributes[0].format = VK_FORMAT_R32G32B32_SFLOAT;
	vertexAttributes[0].offset = 0;

	vertexAttributes[1].binding = 1; 
	vertexAttributes[1].location = 1; 
	vertexAttributes[1].format = VK_FORMAT_R32G32_SFLOAT; 
	vertexAttributes[1].offset = 0;

	vertexAttributes[2].binding = 2; 
	vertexAttributes[2].location = 2; 
	vertexAttributes[2].format = VK_FORMAT_R32G32B32_SFLOAT; 
	vertexAttributes[2].offset = 0;

	VkPipelineVertexInputStateCreateInfo inputInfo{};
	inputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
	inputInfo.vertexBindingDescriptionCount = 3;
	inputInfo.pVertexBindingDescriptions = vertexInputs;
	inputInfo.vertexAttributeDescriptionCount = 3;
	inputInfo.pVertexAttributeDescriptions = vertexAttributes;

	// Define which primitive (point, line, triangle, ...) the input is assembled into for rasterization.
	VkPipelineInputAssemblyStateCreateInfo assemblyInfo{};
	assemblyInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
	assemblyInfo.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
	assemblyInfo.primitiveRestartEnable = VK_FALSE;

	// Define viewport and scissor regions
	VkViewport viewport{};
	viewport.x = 0.f;
	viewport.y = 0.f;
	viewport.width = float(aWindow.swapchainExtent.width);
	viewport.height = float(aWindow.swapchainExtent.height);
	viewport.minDepth = 0.f;
	viewport.maxDepth = 1.f;

	VkRect2D scissor{};
	scissor.offset = VkOffset2D{ 0, 0 };
	scissor.extent = aWindow.swapchainExtent;

	VkPipelineViewportStateCreateInfo viewportInfo{};
	viewportInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
	viewportInfo.viewportCount = 1;
	viewportInfo.pViewports = &viewport;
	viewportInfo.scissorCount = 1;
	viewportInfo.pScissors = &scissor;

	// Define rasterization options
	VkPipelineRasterizationStateCreateInfo rasterInfo{};
	rasterInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
	rasterInfo.depthClampEnable = VK_FALSE;
	rasterInfo.rasterizerDiscardEnable = VK_FALSE;
	rasterInfo.polygonMode = VK_POLYGON_MODE_FILL;
	rasterInfo.cullMode = VK_CULL_MODE_BACK_BIT;
	rasterInfo.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
	rasterInfo.depthBiasEnable = VK_FALSE;
	rasterInfo.lineWidth = 1.f; // required.

	// Define multisampling state
	VkPipelineMultisampleStateCreateInfo samplingInfo{};
	samplingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
	samplingInfo.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

	// Define blend state
	// We define one blend state per color attachment - this example uses a single color attachment, so we only
	// need one. Right now, we don't do any blending, so we can ignore most of the members.
	// Define blend state
	VkPipelineColorBlendAttachmentState blendStates[1]{};
	blendStates[0].blendEnable = VK_FALSE;
	blendStates[0].colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

	VkPipelineColorBlendStateCreateInfo blendInfo{};
	blendInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
	blendInfo.logicOpEnable = VK_FALSE;
	blendInfo.attachmentCount = 1;
	blendInfo.pAttachments = blendStates;

	// Define depth stencil state
	VkPipelineDepthStencilStateCreateInfo depthInfo{};
	depthInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
	depthInfo.depthTestEnable = VK_TRUE;
	depthInfo.depthWriteEnable = VK_TRUE;
	depthInfo.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
	depthInfo.minDepthBounds = 0.f;
	depthInfo.maxDepthBounds = 1.f;

	// Define dynamic states
	VkDynamicState dynamicStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };

	VkPipelineDynamicStateCreateInfo dynamicInfo{};
	dynamicInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
	dynamicInfo.dynamicStateCount = 2;
	dynamicInfo.pDynamicStates = dynamicStates;

	// Pipeline rendering info
	// This is related to dynamic rendering (core in Vulkan 1.3)
	VkPipelineRenderingCreateInfo renderingInfo{};
	renderingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
	renderingInfo.colorAttachmentCount = 1;
	renderingInfo.pColorAttachmentFormats = &aColorFormat;
	renderingInfo.depthAttachmentFormat = cfg::kDepthFormat;

	// Create pipeline
	VkGraphicsPipelineCreateInfo pipeInfo{};
	pipeInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
	pipeInfo.pNext = &renderingInfo; // IMPORTANT! Don't forget!

	pipeInfo.stageCount = 2; // vertex + fragment stages
	pipeInfo.pStages = stages;

	pipeInfo.pVertexInputState = &inputInfo;
	pipeInfo.pInputAssemblyState = &assemblyInfo;
	pipeInfo.pTessellationState = nullptr; // no tessellation
	pipeInfo.pViewportState = &viewportInfo;
	pipeInfo.pRasterizationState = &rasterInfo;
	pipeInfo.pMultisampleState = &samplingInfo;
	pipeInfo.pDepthStencilState = &depthInfo; // UPDATED!
	pipeInfo.pColorBlendState = &blendInfo;
	pipeInfo.pDynamicState = &dynamicInfo; // dynamic states

	pipeInfo.layout = aPipelineLayout;
	pipeInfo.subpass = 0; // first subpass of aRenderPass

	VkPipeline pipe = VK_NULL_HANDLE;
	if( auto const res = vkCreateGraphicsPipelines( aWindow.device, VK_NULL_HANDLE, 1, &pipeInfo, nullptr, &pipe ); VK_SUCCESS != res )
	{
		throw lut::Error( "Unable to create graphics pipeline\n"
			"vkCreateGraphicsPipelines() returned {}", lut::to_string(res)
		);
	}

	return lut::Pipeline( aWindow.device, pipe );
}

lut::Pipeline create_alpha_pipeline( lut::VulkanWindow const& aWindow, VkPipelineLayout aPipelineLayout, VkFormat aColorFormat )
{
	// Load shader code
	auto const vertSpirV = lut::load_file_u32( cfg::kAlphaVertShaderPath );
	auto const fragSpirV = lut::load_file_u32( cfg::kAlphaFragShaderPath );

	VkShaderModuleCreateInfo code[2]{};
	code[0].sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	code[0].codeSize = vertSpirV.size()*sizeof(std::uint32_t);
	code[0].pCode = vertSpirV.data();

	code[1].sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	code[1].codeSize = fragSpirV.size()*sizeof(std::uint32_t);
	code[1].pCode = fragSpirV.data();

	// Define shader stages in the pipeline
	VkPipelineShaderStageCreateInfo stages[2]{};
	stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
	stages[0].pName = "main";
	stages[0].pNext = &code[0];

	stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
	stages[1].pName = "main";
	stages[1].pNext = &code[1];

	VkVertexInputBindingDescription vertexInputs[3]{};
	vertexInputs[0].binding = 0;
	vertexInputs[0].stride = sizeof(float)*3; 
	vertexInputs[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

	vertexInputs[1].binding = 1;
	vertexInputs[1].stride = sizeof(float)*2; 
	vertexInputs[1].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

	vertexInputs[2].binding = 2;
	vertexInputs[2].stride = sizeof(float)*3; 
	vertexInputs[2].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

	VkVertexInputAttributeDescription vertexAttributes[3]{};
	vertexAttributes[0].binding = 0; 
	vertexAttributes[0].location = 0; 
	vertexAttributes[0].format = VK_FORMAT_R32G32B32_SFLOAT;
	vertexAttributes[0].offset = 0;

	vertexAttributes[1].binding = 1; 
	vertexAttributes[1].location = 1; 
	vertexAttributes[1].format = VK_FORMAT_R32G32_SFLOAT; 
	vertexAttributes[1].offset = 0;

	vertexAttributes[2].binding = 2; 
	vertexAttributes[2].location = 2; 
	vertexAttributes[2].format = VK_FORMAT_R32G32B32_SFLOAT; 
	vertexAttributes[2].offset = 0;

	VkPipelineVertexInputStateCreateInfo inputInfo{};
	inputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
	inputInfo.vertexBindingDescriptionCount = 3;
	inputInfo.pVertexBindingDescriptions = vertexInputs;
	inputInfo.vertexAttributeDescriptionCount = 3;
	inputInfo.pVertexAttributeDescriptions = vertexAttributes;

	// Define which primitive (point, line, triangle, ...) the input is assembled into for rasterization.
	VkPipelineInputAssemblyStateCreateInfo assemblyInfo{};
	assemblyInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
	assemblyInfo.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
	assemblyInfo.primitiveRestartEnable = VK_FALSE;

	// Define viewport and scissor regions
	VkViewport viewport{};
	viewport.x = 0.f;
	viewport.y = 0.f;
	viewport.width = float(aWindow.swapchainExtent.width);
	viewport.height = float(aWindow.swapchainExtent.height);
	viewport.minDepth = 0.f;
	viewport.maxDepth = 1.f;

	VkRect2D scissor{};
	scissor.offset = VkOffset2D{ 0, 0 };
	scissor.extent = aWindow.swapchainExtent;

	VkPipelineViewportStateCreateInfo viewportInfo{};
	viewportInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
	viewportInfo.viewportCount = 1;
	viewportInfo.pViewports = &viewport;
	viewportInfo.scissorCount = 1;
	viewportInfo.pScissors = &scissor;

	// Define rasterization options
	VkPipelineRasterizationStateCreateInfo rasterInfo{};
	rasterInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
	rasterInfo.depthClampEnable = VK_FALSE;
	rasterInfo.rasterizerDiscardEnable = VK_FALSE;
	rasterInfo.polygonMode = VK_POLYGON_MODE_FILL;
	rasterInfo.cullMode = VK_CULL_MODE_NONE;
	rasterInfo.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
	rasterInfo.depthBiasEnable = VK_FALSE;
	rasterInfo.lineWidth = 1.f; // required.

	// Define multisampling state
	VkPipelineMultisampleStateCreateInfo samplingInfo{};
	samplingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
	samplingInfo.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

	// Define blend state
	VkPipelineColorBlendAttachmentState blendStates[1]{};
	// blendStates[0].blendEnable = VK_TRUE; // New! Used to be VK FALSE.
	blendStates[0].blendEnable = VK_FALSE; // masking only, no blending
	blendStates[0].colorBlendOp = VK_BLEND_OP_ADD; // New!
	blendStates[0].srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA; // New!
	blendStates[0].dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA; // New!
	blendStates[0].colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

	VkPipelineColorBlendStateCreateInfo blendInfo{};
	blendInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
	blendInfo.logicOpEnable = VK_FALSE;
	blendInfo.attachmentCount = 1;
	blendInfo.pAttachments = blendStates;

	// Define depth stencil state
	VkPipelineDepthStencilStateCreateInfo depthInfo{};
	depthInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
	depthInfo.depthTestEnable = VK_TRUE;
	depthInfo.depthWriteEnable = VK_TRUE;
	depthInfo.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
	depthInfo.minDepthBounds = 0.f;
	depthInfo.maxDepthBounds = 1.f;

	// Define dynamic states
	VkDynamicState dynamicStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };

	VkPipelineDynamicStateCreateInfo dynamicInfo{};
	dynamicInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
	dynamicInfo.dynamicStateCount = 2;
	dynamicInfo.pDynamicStates = dynamicStates;

	// Pipeline rendering info
	// This is related to dynamic rendering (core in Vulkan 1.3)
	VkPipelineRenderingCreateInfo renderingInfo{};
	renderingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
	renderingInfo.colorAttachmentCount = 1;
	renderingInfo.pColorAttachmentFormats = &aColorFormat;
	renderingInfo.depthAttachmentFormat = cfg::kDepthFormat;

	// Create pipeline
	VkGraphicsPipelineCreateInfo pipeInfo{};
	pipeInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
	pipeInfo.pNext = &renderingInfo; // IMPORTANT! Don't forget!

	pipeInfo.stageCount = 2; // vertex + fragment stages
	pipeInfo.pStages = stages;

	pipeInfo.pVertexInputState = &inputInfo;
	pipeInfo.pInputAssemblyState = &assemblyInfo;
	pipeInfo.pTessellationState = nullptr; // no tessellation
	pipeInfo.pViewportState = &viewportInfo;
	pipeInfo.pRasterizationState = &rasterInfo;
	pipeInfo.pMultisampleState = &samplingInfo;
	pipeInfo.pDepthStencilState = &depthInfo; 
	pipeInfo.pColorBlendState = &blendInfo;
	pipeInfo.pDynamicState = &dynamicInfo; // dynamic states

	pipeInfo.layout = aPipelineLayout;
	pipeInfo.subpass = 0; // first subpass of aRenderPass

	VkPipeline pipe = VK_NULL_HANDLE;
	if( auto const res = vkCreateGraphicsPipelines( aWindow.device, VK_NULL_HANDLE, 1, &pipeInfo, nullptr, &pipe ); VK_SUCCESS != res )
	{
		throw lut::Error( "Unable to create graphics pipeline\n"
			"vkCreateGraphicsPipelines() returned {}", lut::to_string(res)
		);
	}

	return lut::Pipeline( aWindow.device, pipe );
}

lut::DescriptorSetLayout create_scene_descriptor_layout( lut::VulkanWindow const& aWindow )
{
	VkDescriptorSetLayoutBinding bindings[2]{};
	bindings[0].binding = 0; // number must match the index of the corresponding binding
	bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	bindings[0].descriptorCount = 1;
	bindings[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

	// p2_1.5 shadow map
	bindings[1].binding = 1;
	bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	bindings[1].descriptorCount = 1;
	bindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

	VkDescriptorSetLayoutCreateInfo layoutInfo{};
	layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	layoutInfo.bindingCount = sizeof(bindings)/sizeof(bindings[0]);
	layoutInfo.pBindings = bindings;

	VkDescriptorSetLayout layout = VK_NULL_HANDLE;
	if( auto const res = vkCreateDescriptorSetLayout( aWindow.device, &layoutInfo, nullptr, &layout ); VK_SUCCESS != res )
	{
		throw lut::Error( "Unable to create descriptor set layout\n"
			"vkCreateDescriptorSetLayout() returned {}", lut::to_string(res)
		);
	}

	return lut::DescriptorSetLayout( aWindow.device, layout );
}
lut::DescriptorSetLayout create_object_descriptor_layout( lut::VulkanWindow const& aWindow )
{
	// bindings for base color, roughness, and metalness
	VkDescriptorSetLayoutBinding bindings[3]{};
	
	bindings[0].binding = 0; 
	bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	bindings[0].descriptorCount = 1;
	bindings[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

	bindings[1].binding = 1; 
	bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	bindings[1].descriptorCount = 1;
	bindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

	bindings[2].binding = 2; 
	bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	bindings[2].descriptorCount = 1;
	bindings[2].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

	VkDescriptorSetLayoutCreateInfo layoutInfo{};
	layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	layoutInfo.bindingCount = sizeof(bindings)/sizeof(bindings[0]);
	layoutInfo.pBindings = bindings;

	VkDescriptorSetLayout layout = VK_NULL_HANDLE;
	if( auto const res = vkCreateDescriptorSetLayout( aWindow.device, &layoutInfo, nullptr, &layout ); VK_SUCCESS != res )
	{
		throw lut::Error( "Unable to create descriptor set layout\n"
			"vkCreateDescriptorSetLayout() returned {}", lut::to_string(res)
		);
	}

	return lut::DescriptorSetLayout( aWindow.device, layout );
}

lut::DescriptorSetLayout create_post_proc_descriptor_layout( lut::VulkanWindow const& aWindow )
{
	VkDescriptorSetLayoutBinding bindings[2]{};
	
	bindings[0].binding = 0; 
	bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	bindings[0].descriptorCount = 1;
	bindings[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

	bindings[1].binding = 1;
	bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	bindings[1].descriptorCount = 1;
	bindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

	VkDescriptorSetLayoutCreateInfo layoutInfo{};
	layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	layoutInfo.bindingCount = 2;
	layoutInfo.pBindings = bindings;


	VkDescriptorSetLayout layout = VK_NULL_HANDLE;
	if( auto const res = vkCreateDescriptorSetLayout( aWindow.device, &layoutInfo, nullptr, &layout ); VK_SUCCESS != res )
	{
		throw lut::Error( "Unable to create descriptor set layout\n"
			"vkCreateDescriptorSetLayout() returned {}", lut::to_string(res)
		);
	}

	return lut::DescriptorSetLayout( aWindow.device, layout );
}

lut::ImageWithView create_depth_buffer( lut::VulkanWindow const& aWindow, lut::Allocator const& aAllocator )
{
	VkImageCreateInfo imageInfo{};
	imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	imageInfo.imageType = VK_IMAGE_TYPE_2D;
	imageInfo.format = cfg::kDepthFormat;
	imageInfo.extent.width = aWindow.swapchainExtent.width;
	imageInfo.extent.height = aWindow.swapchainExtent.height;
	imageInfo.extent.depth = 1;
	imageInfo.mipLevels = 1;
	imageInfo.arrayLayers = 1;
	imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
	imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
	imageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
	imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

	VmaAllocationCreateInfo allocInfo{};
	allocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;

	VkImage image = VK_NULL_HANDLE;
	VmaAllocation allocation = VK_NULL_HANDLE;

	if( auto const res = vmaCreateImage( aAllocator.allocator, &imageInfo, &allocInfo, &image, &allocation, nullptr ); VK_SUCCESS != res )
	{
		throw lut::Error( "Unable to create depth buffer image\n"
			"vmaCreateImage() returned {}", lut::to_string(res)
		);
	}

	VkImageViewCreateInfo viewInfo{};
	viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	viewInfo.image = image;
	viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
	viewInfo.format = cfg::kDepthFormat;
	viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
	viewInfo.subresourceRange.baseMipLevel = 0;
	viewInfo.subresourceRange.levelCount = 1;
	viewInfo.subresourceRange.baseArrayLayer = 0;
	viewInfo.subresourceRange.layerCount = 1;

	VkImageView view = VK_NULL_HANDLE;
	if( auto const res = vkCreateImageView( aWindow.device, &viewInfo, nullptr, &view ); VK_SUCCESS != res )
	{
		vmaDestroyImage( aAllocator.allocator, image, allocation );
		throw lut::Error( "Unable to create depth buffer view\n"
			"vkCreateImageView() returned {}", lut::to_string(res)
		);
	}

	return lut::ImageWithView( aAllocator.allocator, image, allocation, view );
}

lut::ImageWithView create_offscreen_buffer( lut::VulkanWindow const& aWindow, lut::Allocator const& aAllocator )
{
	VkImageCreateInfo imageInfo{};
	imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	imageInfo.imageType = VK_IMAGE_TYPE_2D;
	imageInfo.format = VK_FORMAT_R16G16B16A16_SFLOAT;
	imageInfo.extent.width = aWindow.swapchainExtent.width;
	imageInfo.extent.height = aWindow.swapchainExtent.height;
	imageInfo.extent.depth = 1;
	imageInfo.mipLevels = 1;
	imageInfo.arrayLayers = 1;
	imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
	imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
	imageInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
	imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

	VmaAllocationCreateInfo allocInfo{};
	allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

	VkImage image = VK_NULL_HANDLE;
	VmaAllocation allocation = VK_NULL_HANDLE;

	if( auto const res = vmaCreateImage( aAllocator.allocator, &imageInfo, &allocInfo, &image, &allocation, nullptr ); VK_SUCCESS != res )
	{
		throw lut::Error( "Unable to create offscreen image\n"
			"vmaCreateImage() returned {}", lut::to_string(res)
		);
	}

	VkImageViewCreateInfo viewInfo{};
	viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	viewInfo.image = image;
	viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
	viewInfo.format = VK_FORMAT_R16G16B16A16_SFLOAT;
	viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	viewInfo.subresourceRange.baseMipLevel = 0;
	viewInfo.subresourceRange.levelCount = 1;
	viewInfo.subresourceRange.baseArrayLayer = 0;
	viewInfo.subresourceRange.layerCount = 1;

	VkImageView view = VK_NULL_HANDLE;
	if( auto const res = vkCreateImageView( aWindow.device, &viewInfo, nullptr, &view ); VK_SUCCESS != res )
	{
		vmaDestroyImage( aAllocator.allocator, image, allocation );
		throw lut::Error( "Unable to create offscreen image view\n"
			"vkCreateImageView() returned {}", lut::to_string(res)
		);
	}

	return lut::ImageWithView( aAllocator.allocator, image, allocation, view );
}

// creates a generic pipeline for debug visualization
// the vertex shader is typically the same (debug.vert)
// but the fragment shader depends on keys 1-4
lut::Pipeline create_debug_pipeline( lut::VulkanWindow const& aWindow, VkPipelineLayout aPipelineLayout, char const* aVertPath, char const* aFragPath, VkFormat aColorFormat )
{
	auto const vertSpirV = lut::load_file_u32( aVertPath );
	auto const fragSpirV = lut::load_file_u32( aFragPath );

	VkShaderModuleCreateInfo code[2]{};
	code[0].sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	code[0].codeSize = vertSpirV.size()*sizeof(std::uint32_t);
	code[0].pCode = vertSpirV.data();

	code[1].sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	code[1].codeSize = fragSpirV.size()*sizeof(std::uint32_t);
	code[1].pCode = fragSpirV.data();

	// standard stage setup
	VkPipelineShaderStageCreateInfo stages[2]{};
	stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
	stages[0].pName = "main";
	stages[0].pNext = &code[0];

	stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
	stages[1].pName = "main";
	stages[1].pNext = &code[1];

	// debug pipelines use the same vertex input format as the standard pipeline
	// reuse the same mesh buffers
	VkVertexInputBindingDescription vertexInputs[3]{};
	vertexInputs[0].binding = 0;
	vertexInputs[0].stride = sizeof(float)*3; 
	vertexInputs[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

	vertexInputs[1].binding = 1;
	vertexInputs[1].stride = sizeof(float)*2; 
	vertexInputs[1].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

	vertexInputs[2].binding = 2;
	vertexInputs[2].stride = sizeof(float)*3; 
	vertexInputs[2].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

	VkVertexInputAttributeDescription vertexAttributes[3]{};
	vertexAttributes[0].binding = 0; 
	vertexAttributes[0].location = 0; 
	vertexAttributes[0].format = VK_FORMAT_R32G32B32_SFLOAT;
	vertexAttributes[0].offset = 0;

	vertexAttributes[1].binding = 1; 
	vertexAttributes[1].location = 1; 
	vertexAttributes[1].format = VK_FORMAT_R32G32_SFLOAT; 
	vertexAttributes[1].offset = 0;

	vertexAttributes[2].binding = 2; 
	vertexAttributes[2].location = 2; 
	vertexAttributes[2].format = VK_FORMAT_R32G32B32_SFLOAT; 
	vertexAttributes[2].offset = 0;

	// standard input assembly, viewport, rasterization setup
	VkPipelineVertexInputStateCreateInfo inputInfo{};
	inputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
	inputInfo.vertexBindingDescriptionCount = 3;
	inputInfo.pVertexBindingDescriptions = vertexInputs;
	inputInfo.vertexAttributeDescriptionCount = 3;
	inputInfo.pVertexAttributeDescriptions = vertexAttributes;

	VkPipelineInputAssemblyStateCreateInfo assemblyInfo{};
	assemblyInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
	assemblyInfo.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
	assemblyInfo.primitiveRestartEnable = VK_FALSE;

	VkViewport viewport{};
	viewport.x = 0.f;
	viewport.y = 0.f;
	viewport.width = float(aWindow.swapchainExtent.width);
	viewport.height = float(aWindow.swapchainExtent.height);
	viewport.minDepth = 0.f;
	viewport.maxDepth = 1.f;

	VkRect2D scissor{};
	scissor.offset = VkOffset2D{ 0, 0 };
	scissor.extent = aWindow.swapchainExtent;

	VkPipelineViewportStateCreateInfo viewportInfo{};
	viewportInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
	viewportInfo.viewportCount = 1;
	viewportInfo.pViewports = &viewport;
	viewportInfo.scissorCount = 1;
	viewportInfo.pScissors = &scissor;

	VkPipelineRasterizationStateCreateInfo rasterInfo{};
	rasterInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
	rasterInfo.depthClampEnable = VK_FALSE;
	rasterInfo.rasterizerDiscardEnable = VK_FALSE;
	rasterInfo.polygonMode = VK_POLYGON_MODE_FILL;
	rasterInfo.cullMode = VK_CULL_MODE_BACK_BIT;
	rasterInfo.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
	rasterInfo.depthBiasEnable = VK_FALSE;
	rasterInfo.lineWidth = 1.f;

	VkPipelineMultisampleStateCreateInfo samplingInfo{};
	samplingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
	samplingInfo.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

	// no blending needed for debug output
	// see raw data
	VkPipelineColorBlendAttachmentState blendStates[1]{};
	blendStates[0].blendEnable = VK_FALSE;
	blendStates[0].colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

	VkPipelineColorBlendStateCreateInfo blendInfo{};
	blendInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
	blendInfo.logicOpEnable = VK_FALSE;
	blendInfo.attachmentCount = 1;
	blendInfo.pAttachments = blendStates;

	VkPipelineDepthStencilStateCreateInfo depthInfo{};
	depthInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
	depthInfo.depthTestEnable = VK_TRUE;
	depthInfo.depthWriteEnable = VK_TRUE;
	depthInfo.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
	depthInfo.minDepthBounds = 0.f;
	depthInfo.maxDepthBounds = 1.f;

	VkDynamicState dynamicStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };

	VkPipelineDynamicStateCreateInfo dynamicInfo{};
	dynamicInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
	dynamicInfo.dynamicStateCount = 2;
	dynamicInfo.pDynamicStates = dynamicStates;

	VkPipelineRenderingCreateInfo renderingInfo{};
	renderingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
	renderingInfo.colorAttachmentCount = 1;
	renderingInfo.pColorAttachmentFormats = &aColorFormat;
	renderingInfo.depthAttachmentFormat = cfg::kDepthFormat;

	VkGraphicsPipelineCreateInfo pipeInfo{};
	pipeInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
	pipeInfo.pNext = &renderingInfo; 

	pipeInfo.stageCount = 2; 
	pipeInfo.pStages = stages;

	pipeInfo.pVertexInputState = &inputInfo;
	pipeInfo.pInputAssemblyState = &assemblyInfo;
	pipeInfo.pTessellationState = nullptr;
	pipeInfo.pViewportState = &viewportInfo;
	pipeInfo.pRasterizationState = &rasterInfo;
	pipeInfo.pMultisampleState = &samplingInfo;
	pipeInfo.pDepthStencilState = &depthInfo; 
	pipeInfo.pColorBlendState = &blendInfo;
	pipeInfo.pDynamicState = &dynamicInfo; 

	pipeInfo.layout = aPipelineLayout;
	pipeInfo.subpass = 0; 

	VkPipeline pipe = VK_NULL_HANDLE;
	if( auto const res = vkCreateGraphicsPipelines( aWindow.device, VK_NULL_HANDLE, 1, &pipeInfo, nullptr, &pipe ); VK_SUCCESS != res )
	{
		throw lut::Error( "Unable to create debug graphics pipeline\n"
			"vkCreateGraphicsPipelines() returned {}", lut::to_string(res)
		);
	}

	return lut::Pipeline( aWindow.device, pipe );
}

// creates a dedicated sampler for debug modes (mipmap visual)
// anisotropic filtering is disabled (see mip level transitions)
lut::Sampler create_debug_sampler( lut::VulkanWindow const& aWindow )
{
	VkSamplerCreateInfo samplerInfo{};
	samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
	samplerInfo.magFilter = VK_FILTER_LINEAR;
	samplerInfo.minFilter = VK_FILTER_LINEAR;
	samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
	samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	samplerInfo.mipLodBias = 0.f;
	samplerInfo.anisotropyEnable = VK_FALSE; // task 1.4: disabled for debug visualization to show raw levels
	samplerInfo.maxAnisotropy = 1.f;
	samplerInfo.minLod = 0.f;
	samplerInfo.maxLod = VK_LOD_CLAMP_NONE;
	
	VkSampler sampler = VK_NULL_HANDLE;
	if( auto const res = vkCreateSampler( aWindow.device, &samplerInfo, nullptr, &sampler ); VK_SUCCESS != res )
	{
		throw lut::Error( "Unable to create debug sampler\n"
			"vkCreateSampler() returned {}", lut::to_string(res)
		);
	}

	return lut::Sampler( aWindow.device, sampler );
}

lut::Sampler create_post_proc_sampler( lut::VulkanWindow const& aWindow )
{
	VkSamplerCreateInfo samplerInfo{};
	samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
	samplerInfo.magFilter = VK_FILTER_LINEAR;
	samplerInfo.minFilter = VK_FILTER_LINEAR;
	samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
	samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	samplerInfo.mipLodBias = 0.f;
	samplerInfo.maxAnisotropy = 1.f;
	samplerInfo.minLod = 0.f;
	samplerInfo.maxLod = VK_LOD_CLAMP_NONE;

	VkSampler sampler = VK_NULL_HANDLE;
	if( auto const res = vkCreateSampler( aWindow.device, &samplerInfo, nullptr, &sampler ); VK_SUCCESS != res )
	{
		throw lut::Error( "Unable to create post proc sampler\n"
			"vkCreateSampler() returned {}", lut::to_string(res)
		);
	}

	return lut::Sampler( aWindow.device, sampler );
}

lut::Pipeline create_post_proc_pipeline( lut::VulkanWindow const& aWindow, VkPipelineLayout aPipelineLayout, VkDescriptorSetLayout aDescriptorLayout )
{
	// load shader code
	auto const vertSpirV = lut::load_file_u32( cfg::kFullscreenVertShaderPath );
	auto const fragSpirV = lut::load_file_u32( cfg::kFullscreenFragShaderPath );

	VkShaderModuleCreateInfo code[2]{};
	code[0].sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	code[0].codeSize = vertSpirV.size()*sizeof(std::uint32_t);
	code[0].pCode = vertSpirV.data();

	code[1].sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	code[1].codeSize = fragSpirV.size()*sizeof(std::uint32_t);
	code[1].pCode = fragSpirV.data();

	// define shader stages in the pipeline
	VkPipelineShaderStageCreateInfo stages[2]{};
	stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
	stages[0].pName = "main";
	stages[0].pNext = &code[0];

	stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
	stages[1].pName = "main";
	stages[1].pNext = &code[1];

	// no vertex input generated in shader
	VkPipelineVertexInputStateCreateInfo inputInfo{};
	inputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

	VkPipelineInputAssemblyStateCreateInfo assemblyInfo{};
	assemblyInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
	assemblyInfo.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
	assemblyInfo.primitiveRestartEnable = VK_FALSE;

	VkViewport viewport{};
	viewport.x = 0.f;
	viewport.y = 0.f;
	viewport.width = float(aWindow.swapchainExtent.width);
	viewport.height = float(aWindow.swapchainExtent.height);
	viewport.minDepth = 0.f;
	viewport.maxDepth = 1.f;

	VkRect2D scissor{};
	scissor.offset = VkOffset2D{ 0, 0 };
	scissor.extent = aWindow.swapchainExtent;

	VkPipelineViewportStateCreateInfo viewportInfo{};
	viewportInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
	viewportInfo.viewportCount = 1;
	viewportInfo.pViewports = &viewport;
	viewportInfo.scissorCount = 1;
	viewportInfo.pScissors = &scissor;

	VkPipelineRasterizationStateCreateInfo rasterInfo{};
	rasterInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
	rasterInfo.depthClampEnable = VK_FALSE;
	rasterInfo.rasterizerDiscardEnable = VK_FALSE;
	rasterInfo.polygonMode = VK_POLYGON_MODE_FILL;
	rasterInfo.cullMode = VK_CULL_MODE_NONE; 
	rasterInfo.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
	rasterInfo.depthBiasEnable = VK_FALSE;
	rasterInfo.lineWidth = 1.f;

	VkPipelineMultisampleStateCreateInfo samplingInfo{};
	samplingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
	samplingInfo.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

	VkPipelineColorBlendAttachmentState blendStates[1]{};
	blendStates[0].blendEnable = VK_FALSE;
	blendStates[0].colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

	VkPipelineColorBlendStateCreateInfo blendInfo{};
	blendInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
	blendInfo.logicOpEnable = VK_FALSE;
	blendInfo.attachmentCount = 1;
	blendInfo.pAttachments = blendStates;

	// no depth test needed for full screen quad unless writing depth which we arent
	VkPipelineDepthStencilStateCreateInfo depthInfo{};
	depthInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
	depthInfo.depthTestEnable = VK_FALSE;
	depthInfo.depthWriteEnable = VK_FALSE;
	depthInfo.depthCompareOp = VK_COMPARE_OP_ALWAYS;
	depthInfo.minDepthBounds = 0.f;
	depthInfo.maxDepthBounds = 1.f;

	VkDynamicState dynamicStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };

	VkPipelineDynamicStateCreateInfo dynamicInfo{};
	dynamicInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
	dynamicInfo.dynamicStateCount = 2;
	dynamicInfo.pDynamicStates = dynamicStates;

	VkPipelineRenderingCreateInfo renderingInfo{};
	renderingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
	renderingInfo.colorAttachmentCount = 1;
	renderingInfo.pColorAttachmentFormats = &aWindow.swapchainFormat; 
	
	VkGraphicsPipelineCreateInfo pipeInfo{};
	pipeInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
	pipeInfo.pNext = &renderingInfo;
	pipeInfo.stageCount = 2;
	pipeInfo.pStages = stages;
	pipeInfo.pVertexInputState = &inputInfo;
	pipeInfo.pInputAssemblyState = &assemblyInfo;
	pipeInfo.pTessellationState = nullptr;
	pipeInfo.pViewportState = &viewportInfo;
	pipeInfo.pRasterizationState = &rasterInfo;
	pipeInfo.pMultisampleState = &samplingInfo;
	pipeInfo.pDepthStencilState = &depthInfo;
	pipeInfo.pColorBlendState = &blendInfo;
	pipeInfo.pDynamicState = &dynamicInfo;
	pipeInfo.layout = aPipelineLayout;
	pipeInfo.renderPass = VK_NULL_HANDLE;
	pipeInfo.subpass = 0;

	VkPipeline pipe = VK_NULL_HANDLE;
	if( auto const res = vkCreateGraphicsPipelines( aWindow.device, VK_NULL_HANDLE, 1, &pipeInfo, nullptr, &pipe ); VK_SUCCESS != res )
	{
		throw lut::Error( "Unable to create post proc pipeline\n"
			"vkCreateGraphicsPipelines() returned {}", lut::to_string(res)
		);
	}

	return lut::Pipeline( aWindow.device, pipe );
}

lut::ImageWithView create_vis_image( lut::VulkanWindow const& aWindow, lut::Allocator const& aAllocator )
{
	VkImageCreateInfo imageInfo{};
	imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	imageInfo.imageType = VK_IMAGE_TYPE_2D;
	imageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
	imageInfo.extent.width = aWindow.swapchainExtent.width;
	imageInfo.extent.height = aWindow.swapchainExtent.height;
	imageInfo.extent.depth = 1;
	imageInfo.mipLevels = 1;
	imageInfo.arrayLayers = 1;
	imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
	imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;

	// color attachment (for drawing to) + sampled (for resolving from)
	imageInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
	imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

	VmaAllocationCreateInfo allocInfo{};
	allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

	VkImage image = VK_NULL_HANDLE;
	VmaAllocation allocation = VK_NULL_HANDLE;

	if( auto const res = vmaCreateImage( aAllocator.allocator, &imageInfo, &allocInfo, &image, &allocation, nullptr ); VK_SUCCESS != res )
	{
		throw lut::Error( "Unable to create vis image\n"
			"vmaCreateImage() returned {}", lut::to_string(res)
		);
	}

	VkImageViewCreateInfo viewInfo{};
	viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	viewInfo.image = image;
	viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
	viewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
	viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	viewInfo.subresourceRange.baseMipLevel = 0;
	viewInfo.subresourceRange.levelCount = 1;
	viewInfo.subresourceRange.baseArrayLayer = 0;
	viewInfo.subresourceRange.layerCount = 1;


	VkImageView view = VK_NULL_HANDLE;
	if( auto const res = vkCreateImageView( aWindow.device, &viewInfo, nullptr, &view ); VK_SUCCESS != res )
	{

		vmaDestroyImage( aAllocator.allocator, image, allocation );
		throw lut::Error( "Unable to create vis image view\n"
			"vkCreateImageView() returned {}", lut::to_string(res)
		);
	}

	return lut::ImageWithView( aAllocator.allocator, image, allocation, view );
}

lut::Pipeline create_overdraw_pipeline( lut::VulkanWindow const& aWindow, VkPipelineLayout aPipelineLayout, VkFormat aColorFormat )
{

	auto const vertSpirV = lut::load_file_u32( cfg::kVertShaderPath );
	auto const fragSpirV = lut::load_file_u32( cfg::kOverdrawFragShaderPath );

	VkShaderModuleCreateInfo code[2]{};
	code[0].sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	code[0].codeSize = vertSpirV.size()*sizeof(std::uint32_t);
	code[0].pCode = vertSpirV.data();

	code[1].sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	code[1].codeSize = fragSpirV.size()*sizeof(std::uint32_t);
	code[1].pCode = fragSpirV.data();

	VkPipelineShaderStageCreateInfo stages[2]{};
	stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
	stages[0].pName = "main";
	stages[0].pNext = &code[0];

	stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
	stages[1].pName = "main";
	stages[1].pNext = &code[1];

	VkVertexInputBindingDescription vertexInputs[3]{};
	vertexInputs[0].binding = 0;
	vertexInputs[0].stride = sizeof(float)*3; 
	vertexInputs[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

	vertexInputs[1].binding = 1;
	vertexInputs[1].stride = sizeof(float)*2; 
	vertexInputs[1].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

	vertexInputs[2].binding = 2;
	vertexInputs[2].stride = sizeof(float)*3; 
	vertexInputs[2].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

	VkVertexInputAttributeDescription vertexAttributes[3]{};
	vertexAttributes[0].binding = 0; 
	vertexAttributes[0].location = 0; 
	vertexAttributes[0].format = VK_FORMAT_R32G32B32_SFLOAT;
	vertexAttributes[0].offset = 0;


	vertexAttributes[1].binding = 1; 
	vertexAttributes[1].location = 1; 
	vertexAttributes[1].format = VK_FORMAT_R32G32_SFLOAT; 
	vertexAttributes[1].offset = 0;

	vertexAttributes[2].binding = 2; 
	vertexAttributes[2].location = 2; 
	vertexAttributes[2].format = VK_FORMAT_R32G32B32_SFLOAT; 
	vertexAttributes[2].offset = 0;

	VkPipelineVertexInputStateCreateInfo inputInfo{};
	inputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
	inputInfo.vertexBindingDescriptionCount = 3;
	inputInfo.pVertexBindingDescriptions = vertexInputs;
	inputInfo.vertexAttributeDescriptionCount = 3;
	inputInfo.pVertexAttributeDescriptions = vertexAttributes;

	VkPipelineInputAssemblyStateCreateInfo assemblyInfo{};
	assemblyInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
	assemblyInfo.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
	assemblyInfo.primitiveRestartEnable = VK_FALSE;



	VkViewport viewport{};
	viewport.x = 0.f;
	viewport.y = 0.f;
	viewport.width = float(aWindow.swapchainExtent.width);
	viewport.height = float(aWindow.swapchainExtent.height);
	viewport.minDepth = 0.f;
	viewport.maxDepth = 1.f;

	VkRect2D scissor{};
	scissor.offset = VkOffset2D{ 0, 0 };
	scissor.extent = aWindow.swapchainExtent;

	VkPipelineViewportStateCreateInfo viewportInfo{};
	viewportInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
	viewportInfo.viewportCount = 1;
	viewportInfo.pViewports = &viewport;
	viewportInfo.scissorCount = 1;
	viewportInfo.pScissors = &scissor;
	

	VkPipelineRasterizationStateCreateInfo rasterInfo{};
	rasterInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
	rasterInfo.depthClampEnable = VK_FALSE;
	rasterInfo.rasterizerDiscardEnable = VK_FALSE;
	rasterInfo.polygonMode = VK_POLYGON_MODE_FILL;
	rasterInfo.cullMode = VK_CULL_MODE_BACK_BIT; // Backface culling ENABLED
	rasterInfo.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
	rasterInfo.depthBiasEnable = VK_FALSE;
	rasterInfo.lineWidth = 1.f;



	VkPipelineMultisampleStateCreateInfo samplingInfo{};
	samplingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
	samplingInfo.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

	// Overdraw blend state
	VkPipelineColorBlendAttachmentState blendStates[1]{};
	blendStates[0].blendEnable = VK_TRUE; 
	blendStates[0].colorBlendOp = VK_BLEND_OP_ADD;
	blendStates[0].srcColorBlendFactor = VK_BLEND_FACTOR_ONE; 
	blendStates[0].dstColorBlendFactor = VK_BLEND_FACTOR_ONE; 
	// no alpha write
	blendStates[0].colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT;

	VkPipelineColorBlendStateCreateInfo blendInfo{};
	blendInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
	blendInfo.logicOpEnable = VK_FALSE;
	blendInfo.attachmentCount = 1;
	blendInfo.pAttachments = blendStates;

	// Overdraw depth state: test off, write off
	VkPipelineDepthStencilStateCreateInfo depthInfo{};
	depthInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
	depthInfo.depthTestEnable = VK_FALSE;
	depthInfo.depthWriteEnable = VK_FALSE;
	depthInfo.depthCompareOp = VK_COMPARE_OP_ALWAYS;
	depthInfo.minDepthBounds = 0.f;
	depthInfo.maxDepthBounds = 1.f;

	VkDynamicState dynamicStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };

	VkPipelineDynamicStateCreateInfo dynamicInfo{};
	dynamicInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
	dynamicInfo.dynamicStateCount = 2;
	dynamicInfo.pDynamicStates = dynamicStates;

	VkPipelineRenderingCreateInfo renderingInfo{};
	renderingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
	renderingInfo.colorAttachmentCount = 1;
	renderingInfo.pColorAttachmentFormats = &aColorFormat;
	// no depth attachment for overdraw calculation
	// binding for safety
	// pass D32 format
	renderingInfo.depthAttachmentFormat = cfg::kDepthFormat;

	VkGraphicsPipelineCreateInfo pipeInfo{};
	pipeInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
	pipeInfo.pNext = &renderingInfo; 
	pipeInfo.stageCount = 2; 
	pipeInfo.pStages = stages;
	pipeInfo.pVertexInputState = &inputInfo;
	pipeInfo.pInputAssemblyState = &assemblyInfo;
	pipeInfo.pTessellationState = nullptr;
	pipeInfo.pViewportState = &viewportInfo;
	pipeInfo.pRasterizationState = &rasterInfo;
	pipeInfo.pMultisampleState = &samplingInfo;
	pipeInfo.pDepthStencilState = &depthInfo; 
	pipeInfo.pColorBlendState = &blendInfo;
	pipeInfo.pDynamicState = &dynamicInfo; 
	pipeInfo.layout = aPipelineLayout;
	pipeInfo.subpass = 0; 

	VkPipeline pipe = VK_NULL_HANDLE;
	if( auto const res = vkCreateGraphicsPipelines( aWindow.device, VK_NULL_HANDLE, 1, &pipeInfo, nullptr, &pipe ); VK_SUCCESS != res )
	{
		throw lut::Error( "Unable to create overdraw graphics pipeline\n"
			"vkCreateGraphicsPipelines() returned {}", lut::to_string(res)
		);

	}

	return lut::Pipeline( aWindow.device, pipe );
}

lut::Pipeline create_overshading_pipeline( lut::VulkanWindow const& aWindow, VkPipelineLayout aPipelineLayout, VkFormat aColorFormat )
{

	auto const vertSpirV = lut::load_file_u32( cfg::kVertShaderPath );
	auto const fragSpirV = lut::load_file_u32( cfg::kOverdrawFragShaderPath );

	VkShaderModuleCreateInfo code[2]{};
	code[0].sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	code[0].codeSize = vertSpirV.size()*sizeof(std::uint32_t);
	code[0].pCode = vertSpirV.data();

	code[1].sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	code[1].codeSize = fragSpirV.size()*sizeof(std::uint32_t);
	code[1].pCode = fragSpirV.data();

	VkPipelineShaderStageCreateInfo stages[2]{};
	stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
	stages[0].pName = "main";
	stages[0].pNext = &code[0];


	stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
	stages[1].pName = "main";
	stages[1].pNext = &code[1];

	VkVertexInputBindingDescription vertexInputs[3]{};
	vertexInputs[0].binding = 0;
	vertexInputs[0].stride = sizeof(float)*3; 
	vertexInputs[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

	vertexInputs[1].binding = 1;
	vertexInputs[1].stride = sizeof(float)*2; 
	vertexInputs[1].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

	vertexInputs[2].binding = 2;
	vertexInputs[2].stride = sizeof(float)*3; 
	vertexInputs[2].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

	VkVertexInputAttributeDescription vertexAttributes[3]{};
	vertexAttributes[0].binding = 0; 
	vertexAttributes[0].location = 0; 
	vertexAttributes[0].format = VK_FORMAT_R32G32B32_SFLOAT;
	vertexAttributes[0].offset = 0;

	vertexAttributes[1].binding = 1; 
	vertexAttributes[1].location = 1; 
	vertexAttributes[1].format = VK_FORMAT_R32G32_SFLOAT; 
	vertexAttributes[1].offset = 0;

	vertexAttributes[2].binding = 2; 
	vertexAttributes[2].location = 2; 
	vertexAttributes[2].format = VK_FORMAT_R32G32B32_SFLOAT; 
	vertexAttributes[2].offset = 0;

	VkPipelineVertexInputStateCreateInfo inputInfo{};
	inputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
	inputInfo.vertexBindingDescriptionCount = 3;
	inputInfo.pVertexBindingDescriptions = vertexInputs;
	inputInfo.vertexAttributeDescriptionCount = 3;
	inputInfo.pVertexAttributeDescriptions = vertexAttributes;

	VkPipelineInputAssemblyStateCreateInfo assemblyInfo{};
	assemblyInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
	assemblyInfo.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
	assemblyInfo.primitiveRestartEnable = VK_FALSE;

	VkViewport viewport{};
	viewport.x = 0.f;
	viewport.y = 0.f;
	viewport.width = float(aWindow.swapchainExtent.width);
	viewport.height = float(aWindow.swapchainExtent.height);
	viewport.minDepth = 0.f;
	viewport.maxDepth = 1.f;

	VkRect2D scissor{};
	scissor.offset = VkOffset2D{ 0, 0 };
	scissor.extent = aWindow.swapchainExtent;

	VkPipelineViewportStateCreateInfo viewportInfo{};
	viewportInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
	viewportInfo.viewportCount = 1;
	viewportInfo.pViewports = &viewport;
	viewportInfo.scissorCount = 1;
	viewportInfo.pScissors = &scissor;

	VkPipelineRasterizationStateCreateInfo rasterInfo{};
	rasterInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
	rasterInfo.depthClampEnable = VK_FALSE;
	rasterInfo.rasterizerDiscardEnable = VK_FALSE;
	rasterInfo.polygonMode = VK_POLYGON_MODE_FILL;
	rasterInfo.cullMode = VK_CULL_MODE_BACK_BIT;
	rasterInfo.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
	rasterInfo.depthBiasEnable = VK_FALSE;
	rasterInfo.lineWidth = 1.f;

	VkPipelineMultisampleStateCreateInfo samplingInfo{};
	samplingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
	samplingInfo.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

	// Overshading blend state
	VkPipelineColorBlendAttachmentState blendStates[1]{};
	blendStates[0].blendEnable = VK_TRUE; 
	blendStates[0].colorBlendOp = VK_BLEND_OP_ADD;
	blendStates[0].srcColorBlendFactor = VK_BLEND_FACTOR_ONE; 
	blendStates[0].dstColorBlendFactor = VK_BLEND_FACTOR_ONE; 
	// No alpha write
	blendStates[0].colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT;

	VkPipelineColorBlendStateCreateInfo blendInfo{};
	blendInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
	blendInfo.logicOpEnable = VK_FALSE;
	blendInfo.attachmentCount = 1;
	blendInfo.pAttachments = blendStates;

	// Overshading depth state: test On (LESS), write On
	VkPipelineDepthStencilStateCreateInfo depthInfo{};
	depthInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
	depthInfo.depthTestEnable = VK_TRUE;
	depthInfo.depthWriteEnable = VK_TRUE;
	depthInfo.depthCompareOp = VK_COMPARE_OP_LESS; // Critical for Overshading; all in hehe
	depthInfo.minDepthBounds = 0.f;
	depthInfo.maxDepthBounds = 1.f;

	VkDynamicState dynamicStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };

	VkPipelineDynamicStateCreateInfo dynamicInfo{};
	dynamicInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
	dynamicInfo.dynamicStateCount = 2;
	dynamicInfo.pDynamicStates = dynamicStates;

	VkPipelineRenderingCreateInfo renderingInfo{};
	renderingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
	renderingInfo.colorAttachmentCount = 1;
	renderingInfo.pColorAttachmentFormats = &aColorFormat;
	renderingInfo.depthAttachmentFormat = cfg::kDepthFormat;

	VkGraphicsPipelineCreateInfo pipeInfo{};
	pipeInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
	pipeInfo.pNext = &renderingInfo; 
	pipeInfo.stageCount = 2; 
	pipeInfo.pStages = stages;
	pipeInfo.pVertexInputState = &inputInfo;
	pipeInfo.pInputAssemblyState = &assemblyInfo;
	pipeInfo.pTessellationState = nullptr;
	pipeInfo.pViewportState = &viewportInfo;
	pipeInfo.pRasterizationState = &rasterInfo;
	pipeInfo.pMultisampleState = &samplingInfo;
	pipeInfo.pDepthStencilState = &depthInfo; 
	pipeInfo.pColorBlendState = &blendInfo;
	pipeInfo.pDynamicState = &dynamicInfo; 
	pipeInfo.layout = aPipelineLayout;
	pipeInfo.subpass = 0; 


	VkPipeline pipe = VK_NULL_HANDLE;
	if( auto const res = vkCreateGraphicsPipelines( aWindow.device, VK_NULL_HANDLE, 1, &pipeInfo, nullptr, &pipe ); VK_SUCCESS != res )
	{
		throw lut::Error( "Unable to create overshading graphics pipeline\n"
			"vkCreateGraphicsPipelines() returned {}", lut::to_string(res)
		);
	}

	return lut::Pipeline( aWindow.device, pipe );
}

lut::Pipeline create_vis_resolve_pipeline( lut::VulkanWindow const& aWindow, VkPipelineLayout aPipelineLayout, VkDescriptorSetLayout aDescriptorLayout )
{

	auto const vertSpirV = lut::load_file_u32( cfg::kFullscreenVertShaderPath );
	auto const fragSpirV = lut::load_file_u32( cfg::kPassthroughFragShaderPath );

	VkShaderModuleCreateInfo code[2]{};
	code[0].sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	code[0].codeSize = vertSpirV.size()*sizeof(std::uint32_t);
	code[0].pCode = vertSpirV.data();

	code[1].sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	code[1].codeSize = fragSpirV.size()*sizeof(std::uint32_t);
	code[1].pCode = fragSpirV.data();

	VkPipelineShaderStageCreateInfo stages[2]{};
	stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
	stages[0].pName = "main";
	stages[0].pNext = &code[0];


	stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
	stages[1].pName = "main";
	stages[1].pNext = &code[1];

	VkPipelineVertexInputStateCreateInfo inputInfo{};
	inputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

	VkPipelineInputAssemblyStateCreateInfo assemblyInfo{};
	assemblyInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
	assemblyInfo.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
	assemblyInfo.primitiveRestartEnable = VK_FALSE;

	VkViewport viewport{};
	viewport.x = 0.f;
	viewport.y = 0.f;
	viewport.width = float(aWindow.swapchainExtent.width);
	viewport.height = float(aWindow.swapchainExtent.height);
	viewport.minDepth = 0.f;
	viewport.maxDepth = 1.f;

	VkRect2D scissor{};
	scissor.offset = VkOffset2D{ 0, 0 };
	scissor.extent = aWindow.swapchainExtent;

	VkPipelineViewportStateCreateInfo viewportInfo{};
	viewportInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
	viewportInfo.viewportCount = 1;
	viewportInfo.pViewports = &viewport;
	viewportInfo.scissorCount = 1;
	viewportInfo.pScissors = &scissor;

	VkPipelineRasterizationStateCreateInfo rasterInfo{};
	rasterInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
	rasterInfo.depthClampEnable = VK_FALSE;
	rasterInfo.rasterizerDiscardEnable = VK_FALSE;
	rasterInfo.polygonMode = VK_POLYGON_MODE_FILL;
	rasterInfo.cullMode = VK_CULL_MODE_NONE; 
	rasterInfo.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
	rasterInfo.depthBiasEnable = VK_FALSE;
	rasterInfo.lineWidth = 1.f;

	VkPipelineMultisampleStateCreateInfo samplingInfo{};
	samplingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
	samplingInfo.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

	VkPipelineColorBlendAttachmentState blendStates[1]{};
	blendStates[0].blendEnable = VK_FALSE;
	blendStates[0].colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

	VkPipelineColorBlendStateCreateInfo blendInfo{};
	blendInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
	blendInfo.logicOpEnable = VK_FALSE;
	blendInfo.attachmentCount = 1;
	blendInfo.pAttachments = blendStates;

	VkPipelineDepthStencilStateCreateInfo depthInfo{};
	depthInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
	depthInfo.depthTestEnable = VK_FALSE;
	depthInfo.depthWriteEnable = VK_FALSE;
	depthInfo.depthCompareOp = VK_COMPARE_OP_ALWAYS;
	depthInfo.minDepthBounds = 0.f;
	depthInfo.maxDepthBounds = 1.f;

	VkDynamicState dynamicStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };

	VkPipelineDynamicStateCreateInfo dynamicInfo{};
	dynamicInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
	dynamicInfo.dynamicStateCount = 2;
	dynamicInfo.pDynamicStates = dynamicStates;

	VkPipelineRenderingCreateInfo renderingInfo{};
	renderingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
	renderingInfo.colorAttachmentCount = 1;
	renderingInfo.colorAttachmentCount = 1;
	
	// need UNORM 
	renderingInfo.pColorAttachmentFormats = &aWindow.swapchainFormat; 
	
	VkGraphicsPipelineCreateInfo pipeInfo{};
	pipeInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
	pipeInfo.pNext = &renderingInfo;
	pipeInfo.stageCount = 2;
	pipeInfo.pStages = stages;
	pipeInfo.pVertexInputState = &inputInfo;
	pipeInfo.pInputAssemblyState = &assemblyInfo;
	pipeInfo.pTessellationState = nullptr;
	pipeInfo.pViewportState = &viewportInfo;
	pipeInfo.pRasterizationState = &rasterInfo;
	pipeInfo.pMultisampleState = &samplingInfo;
	pipeInfo.pDepthStencilState = &depthInfo;
	pipeInfo.pColorBlendState = &blendInfo;
	pipeInfo.pDynamicState = &dynamicInfo;
	pipeInfo.layout = aPipelineLayout;
	pipeInfo.renderPass = VK_NULL_HANDLE;
	pipeInfo.subpass = 0;

	VkPipeline pipe = VK_NULL_HANDLE;
	if( auto const res = vkCreateGraphicsPipelines( aWindow.device, VK_NULL_HANDLE, 1, &pipeInfo, nullptr, &pipe ); VK_SUCCESS != res )
	{
		// error
		throw lut::Error( "Unable to create vis resolve pipeline\n"
			"vkCreateGraphicsPipelines() returned {}", lut::to_string(res)
		);
	}

	return lut::Pipeline( aWindow.device, pipe );
}

// p2_1.5 shadow sampler
lut::Sampler create_shadow_sampler( lut::VulkanWindow const& aWindow )
{
	VkSamplerCreateInfo samplerInfo{};
	samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
	samplerInfo.magFilter = VK_FILTER_LINEAR;
	samplerInfo.minFilter = VK_FILTER_LINEAR;
	samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST; // no mipmaps for shadow map
	samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
	samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
	samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
	samplerInfo.mipLodBias = 0.f;
	samplerInfo.anisotropyEnable = VK_FALSE;
	samplerInfo.minLod = 0.f;
	samplerInfo.maxLod = 1000.f;
	samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
	// outside shadow map = 1.0 (lit)
	samplerInfo.compareEnable = VK_TRUE;
	samplerInfo.compareOp = VK_COMPARE_OP_LESS;
	// shadow test: ref < texture ?
	samplerInfo.unnormalizedCoordinates = VK_FALSE;

	VkSampler sampler = VK_NULL_HANDLE;
	if( auto const res = vkCreateSampler( aWindow.device, &samplerInfo, nullptr, &sampler ); VK_SUCCESS != res )
	{
		throw lut::Error( "Unable to create shadow sampler\n"
			"vkCreateSampler() returned {}", lut::to_string(res)
		);
	}

	return lut::Sampler( aWindow.device, sampler );
}

lut::ImageWithView create_shadow_map( lut::VulkanWindow const& aWindow, lut::Allocator const& aAllocator )
{
	// p2_1.5 high resolution shadow map
	VkImageCreateInfo imageInfo{};
	imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	imageInfo.imageType = VK_IMAGE_TYPE_2D;
	imageInfo.format = cfg::kShadowMapFormat; // D32_SFLOAT
	imageInfo.extent.width = kShadowMapResolution; // resolution goes brrrr
	imageInfo.extent.height = kShadowMapResolution;
	imageInfo.extent.depth = 1;
	imageInfo.mipLevels = 1;
	imageInfo.arrayLayers = 1;
	imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
	imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
	imageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
	imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;


	VmaAllocationCreateInfo allocInfo{};
	allocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;

	VkImage image = VK_NULL_HANDLE;
	VmaAllocation allocation = VK_NULL_HANDLE;

	if( auto const res = vmaCreateImage( aAllocator.allocator, &imageInfo, &allocInfo, &image, &allocation, nullptr ); VK_SUCCESS != res )
	{

		throw lut::Error( "Unable to create shadow map image\n"
			"vmaCreateImage() returned {}", lut::to_string(res)
		);
	}

	VkImageViewCreateInfo viewInfo{};
	viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	viewInfo.image = image;
	viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
	viewInfo.format = cfg::kShadowMapFormat;
	viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT; // strict depth aspect
	viewInfo.subresourceRange.baseMipLevel = 0;
	viewInfo.subresourceRange.levelCount = 1;
	viewInfo.subresourceRange.baseArrayLayer = 0;
	viewInfo.subresourceRange.layerCount = 1;

	VkImageView view = VK_NULL_HANDLE;
	if( auto const res = vkCreateImageView( aWindow.device, &viewInfo, nullptr, &view ); VK_SUCCESS != res )
	{

		vmaDestroyImage( aAllocator.allocator, image, allocation );
		throw lut::Error( "Unable to create shadow map view\n"
			"vkCreateImageView() returned {}", lut::to_string(res)
		);
	}

	return lut::ImageWithView( aAllocator.allocator, image, allocation, view );


}

lut::Pipeline create_shadow_pipeline( lut::VulkanWindow const& aWindow, VkPipelineLayout aPipelineLayout )
{
	// Load shader code
	auto const vertSpirV = lut::load_file_u32( cfg::kShadowVertShaderPath );
	auto const fragSpirV = lut::load_file_u32( cfg::kShadowFragShaderPath );

	VkShaderModuleCreateInfo code[2]{};
	code[0].sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	code[0].codeSize = vertSpirV.size()*sizeof(std::uint32_t);
	code[0].pCode = vertSpirV.data();

	code[1].sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	code[1].codeSize = fragSpirV.size()*sizeof(std::uint32_t);
	code[1].pCode = fragSpirV.data();

	VkPipelineShaderStageCreateInfo stages[2]{};
	stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
	stages[0].pName = "main";
	stages[0].pNext = &code[0];

	stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
	stages[1].pName = "main";
	stages[1].pNext = &code[1];

	// reuse generic vertex inputs
	// remove Normals from shadow pipeline
	VkVertexInputBindingDescription vertexInputs[2]{}; // 2 bindings
	vertexInputs[0].binding = 0;
	vertexInputs[0].stride = sizeof(float)*3; 
	vertexInputs[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

	vertexInputs[1].binding = 1;
	vertexInputs[1].stride = sizeof(float)*2; 
	vertexInputs[1].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

	// No binding 2

	VkVertexInputAttributeDescription vertexAttributes[2]{}; // 2 attributes
	vertexAttributes[0].binding = 0; 
	vertexAttributes[0].location = 0; 
	vertexAttributes[0].format = VK_FORMAT_R32G32B32_SFLOAT;
	vertexAttributes[0].offset = 0;

	vertexAttributes[1].binding = 1; 
	vertexAttributes[1].location = 1; 
	vertexAttributes[1].format = VK_FORMAT_R32G32_SFLOAT; 
	vertexAttributes[1].offset = 0;

	// No location 2

	VkPipelineVertexInputStateCreateInfo inputInfo{};
	inputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
	inputInfo.vertexBindingDescriptionCount = 2; // Reduced
	inputInfo.pVertexBindingDescriptions = vertexInputs;
	inputInfo.vertexAttributeDescriptionCount = 2; // Reduced
	inputInfo.pVertexAttributeDescriptions = vertexAttributes;

	VkPipelineInputAssemblyStateCreateInfo assemblyInfo{};
	assemblyInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
	assemblyInfo.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
	assemblyInfo.primitiveRestartEnable = VK_FALSE;

	// viewport and scissor - will be dynamic
	// placeholders
	VkViewport viewport{};
	VkRect2D scissor{};

	VkPipelineViewportStateCreateInfo viewportInfo{};
	viewportInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
	viewportInfo.viewportCount = 1;
	viewportInfo.pViewports = &viewport;
	viewportInfo.scissorCount = 1;
	viewportInfo.pScissors = &scissor;

	// Rasterization
	VkPipelineRasterizationStateCreateInfo rasterInfo{};
	rasterInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
	rasterInfo.depthClampEnable = VK_FALSE;
	rasterInfo.rasterizerDiscardEnable = VK_FALSE;
	rasterInfo.polygonMode = VK_POLYGON_MODE_FILL;
	rasterInfo.cullMode = VK_CULL_MODE_NONE;
	rasterInfo.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
	rasterInfo.depthBiasEnable = VK_TRUE; // ENABLE DEPTH BIAS
	rasterInfo.lineWidth = 1.f;

	VkPipelineMultisampleStateCreateInfo samplingInfo{};
	samplingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
	samplingInfo.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

	// No color attachment
	VkPipelineColorBlendStateCreateInfo blendInfo{};
	blendInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
	blendInfo.logicOpEnable = VK_FALSE;
	blendInfo.attachmentCount = 0; // No color attachment
	blendInfo.pAttachments = nullptr;

	VkPipelineDepthStencilStateCreateInfo depthInfo{};
	depthInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
	depthInfo.depthTestEnable = VK_TRUE; // TEST
	depthInfo.depthWriteEnable = VK_TRUE; // WRITE
	depthInfo.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL; 
	depthInfo.minDepthBounds = 0.f;
	depthInfo.maxDepthBounds = 1.f;

	VkDynamicState dynamicStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR, VK_DYNAMIC_STATE_DEPTH_BIAS };
	VkPipelineDynamicStateCreateInfo dynamicInfo{};
	dynamicInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
	dynamicInfo.dynamicStateCount = 3;
	dynamicInfo.pDynamicStates = dynamicStates;

	VkPipelineRenderingCreateInfo renderingInfo{};
	renderingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
	renderingInfo.colorAttachmentCount = 0; // No color attachment
	renderingInfo.pColorAttachmentFormats = nullptr;
	renderingInfo.depthAttachmentFormat = cfg::kShadowMapFormat;

	VkGraphicsPipelineCreateInfo pipeInfo{};
	pipeInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
	pipeInfo.pNext = &renderingInfo;

	pipeInfo.stageCount = 2; // vert + frag; alpha discard
	pipeInfo.pStages = stages;

	pipeInfo.pVertexInputState = &inputInfo;
	pipeInfo.pInputAssemblyState = &assemblyInfo;
	pipeInfo.pTessellationState = nullptr;
	pipeInfo.pViewportState = &viewportInfo;
	pipeInfo.pRasterizationState = &rasterInfo;
	pipeInfo.pMultisampleState = &samplingInfo;
	pipeInfo.pDepthStencilState = &depthInfo;
	pipeInfo.pColorBlendState = &blendInfo;
	pipeInfo.pDynamicState = &dynamicInfo;

	pipeInfo.layout = aPipelineLayout;
	pipeInfo.subpass = 0;


	VkPipeline pipe = VK_NULL_HANDLE;
	if( auto const res = vkCreateGraphicsPipelines( aWindow.device, VK_NULL_HANDLE, 1, &pipeInfo, nullptr, &pipe ); VK_SUCCESS != res )
	{
		throw lut::Error( "Unable to create shadow pipeline\n"
			"vkCreateGraphicsPipelines() returned {}", lut::to_string(res)
		);
		
	}

	return lut::Pipeline( aWindow.device, pipe );
}


