#include "setup.hpp"

#include "../../Rhi/error.hpp"
#include "../../Rhi/to_string.hpp"
#include "../../Rhi/load.hpp"


#include <glm/glm.hpp>
#include <glm/gtx/transform.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <cstddef>
#include "../Particle/ParticleSystem.hpp"
#include "../Debug/DebugRenderer.hpp"

lut::PipelineLayout create_triangle_pipeline_layout( lut::VulkanContext const& aContext, VkDescriptorSetLayout aSceneLayout, VkDescriptorSetLayout aObjectLayout )
{
	VkDescriptorSetLayout layouts[] = {
		// Order must match the set = N in the shaders
		aSceneLayout, // set 0
		aObjectLayout // set 1
	};

	//matrix calculate
	VkPushConstantRange pushConstant{};
	pushConstant.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
	pushConstant.offset = 0;
	pushConstant.size = 128;
	//pushConstant.size = sizeof(glm::mat4); 


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

lut::PipelineLayout create_composite_pipeline_layout(lut::VulkanContext const& aContext, VkDescriptorSetLayout aDescriptorLayout)
{
	VkPushConstantRange pushConstant{};
	pushConstant.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
	pushConstant.offset = 0;
	pushConstant.size = 16; // BloomPC

	VkDescriptorSetLayout layouts[] = {
		aDescriptorLayout // set 0
	};

	VkPipelineLayoutCreateInfo layoutInfo{};
	layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	layoutInfo.setLayoutCount = 1;
	layoutInfo.pSetLayouts = layouts;
	layoutInfo.pushConstantRangeCount = 1;
	layoutInfo.pPushConstantRanges = &pushConstant;

	VkPipelineLayout layout = VK_NULL_HANDLE;
	if (auto const res = vkCreatePipelineLayout(aContext.device, &layoutInfo, nullptr, &layout); VK_SUCCESS != res)
	{
		throw lut::Error("Unable to create composite pipeline layout\n"
			"vkCreatePipelineLayout() returned {}", lut::to_string(res)
		);
	}

	return lut::PipelineLayout(aContext.device, layout);
}
lut::PipelineLayout create_blur_pipeline_layout(lut::VulkanContext const& aContext, VkDescriptorSetLayout aDescriptorLayout)
{
	// 定义 Push Constant 范围
	// 我们只需要一�?int (或�?bool) 来表�?horizontal 状�?
	VkPushConstantRange pushConstant{};
	pushConstant.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
	pushConstant.offset = 0;
	pushConstant.size = sizeof(int32_t); // 传递一�?4 字节的整�?

	VkDescriptorSetLayout layouts[] = {
		aDescriptorLayout // set 0: 包含输入的采样器纹理
	};

	VkPipelineLayoutCreateInfo layoutInfo{};
	layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	layoutInfo.setLayoutCount = 1;
	layoutInfo.pSetLayouts = layouts;
	layoutInfo.pushConstantRangeCount = 1;
	layoutInfo.pPushConstantRanges = &pushConstant;

	VkPipelineLayout layout = VK_NULL_HANDLE;
	if (auto const res = vkCreatePipelineLayout(aContext.device, &layoutInfo, nullptr, &layout); VK_SUCCESS != res)
	{
		throw lut::Error("Unable to create blur pipeline layout\n"
			"vkCreatePipelineLayout() returned {}", lut::to_string(res)
		);
	}

	return lut::PipelineLayout(aContext.device, layout);
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

	VkVertexInputBindingDescription vertexInputs[5]{};
	vertexInputs[0].binding = 0; vertexInputs[0].stride = sizeof(float) * 3; vertexInputs[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
	vertexInputs[1].binding = 1; vertexInputs[1].stride = sizeof(float) * 2; vertexInputs[1].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
	vertexInputs[2].binding = 2; vertexInputs[2].stride = sizeof(float) * 3; vertexInputs[2].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
	vertexInputs[3].binding = 3; vertexInputs[3].stride = sizeof(uint32_t) * 4; vertexInputs[3].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
	vertexInputs[4].binding = 4; vertexInputs[4].stride = sizeof(float) * 4; vertexInputs[4].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

	VkVertexInputAttributeDescription vertexAttributes[5]{};
	vertexAttributes[0].binding = 0; vertexAttributes[0].location = 0; vertexAttributes[0].format = VK_FORMAT_R32G32B32_SFLOAT; vertexAttributes[0].offset = 0;
	vertexAttributes[1].binding = 1; vertexAttributes[1].location = 1; vertexAttributes[1].format = VK_FORMAT_R32G32_SFLOAT; vertexAttributes[1].offset = 0;
	vertexAttributes[2].binding = 2; vertexAttributes[2].location = 2; vertexAttributes[2].format = VK_FORMAT_R32G32B32_SFLOAT; vertexAttributes[2].offset = 0;
	vertexAttributes[3].binding = 3; vertexAttributes[3].location = 3; vertexAttributes[3].format = VK_FORMAT_R32G32B32A32_UINT; vertexAttributes[3].offset = 0;
	vertexAttributes[4].binding = 4; vertexAttributes[4].location = 4; vertexAttributes[4].format = VK_FORMAT_R32G32B32A32_SFLOAT; vertexAttributes[4].offset = 0;

	VkPipelineVertexInputStateCreateInfo inputInfo{};
	inputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
	inputInfo.vertexBindingDescriptionCount = 5;
	inputInfo.pVertexBindingDescriptions = vertexInputs;
	inputInfo.vertexAttributeDescriptionCount = 5;
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
	// We define one blend state per color attachment - this example uses a single color attachment, so we only
	// need one. Right now, we don't do any blending, so we can ignore most of the members.
	// Define blend state
	VkPipelineColorBlendAttachmentState blendStates[2]{};
	// Attachment 0: 主颜�?(Main Color)
	blendStates[0].blendEnable = VK_FALSE;
	blendStates[0].colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

	// Attachment 1: 亮度提取 (Bright Color)
	blendStates[1].blendEnable = VK_FALSE;
	blendStates[1].colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

	VkPipelineColorBlendStateCreateInfo blendInfo{};
	blendInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
	blendInfo.logicOpEnable = VK_FALSE;
	blendInfo.attachmentCount = 2;
	blendInfo.pAttachments = blendStates;
	// 4. 【关键修复】渲染信�?(Dynamic Rendering)
	// 定义格式数组，确保在创建 Pipeline 时内存有�?
	VkFormat colorFormats[] = {
		VK_FORMAT_R16G16B16A16_SFLOAT,                    // Location 0: 正常颜色 (通常�?Swapchain 格式)
		VK_FORMAT_R16G16B16A16_SFLOAT        // Location 1: Bloom 亮度�?(必须�?HDR 格式)
	};

	VkPipelineRenderingCreateInfo renderingInfo{};
	renderingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
	renderingInfo.colorAttachmentCount = 2;      // 必须�?2
	renderingInfo.pColorAttachmentFormats = colorFormats; // 指向数组首地址
	renderingInfo.depthAttachmentFormat = cfg::kDepthFormat;
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
	//VkPipelineRenderingCreateInfo renderingInfo{};
	//renderingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
	//renderingInfo.colorAttachmentCount = 2;
	//renderingInfo.pColorAttachmentFormats = &aColorFormat;
	//renderingInfo.depthAttachmentFormat = cfg::kDepthFormat;

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

lut::Pipeline create_alpha_pipeline(lut::VulkanWindow const& aWindow, VkPipelineLayout aPipelineLayout, VkFormat aColorFormat)
{
	// =================================================================
	// 1. 【核心修复】复用正常场景的 Shader！旧�?alpha shader 读不到颜色！
	// =================================================================
	auto const vertSpirV = lut::load_file_u32(cfg::kVertShaderPath);
	auto const fragSpirV = lut::load_file_u32(cfg::kFragShaderPath);

	VkShaderModuleCreateInfo code[2]{};
	code[0].sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	code[0].codeSize = vertSpirV.size() * sizeof(std::uint32_t);
	code[0].pCode = vertSpirV.data();

	code[1].sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	code[1].codeSize = fragSpirV.size() * sizeof(std::uint32_t);
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

	VkVertexInputBindingDescription vertexInputs[5]{};
	vertexInputs[0].binding = 0; vertexInputs[0].stride = sizeof(float) * 3; vertexInputs[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
	vertexInputs[1].binding = 1; vertexInputs[1].stride = sizeof(float) * 2; vertexInputs[1].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
	vertexInputs[2].binding = 2; vertexInputs[2].stride = sizeof(float) * 3; vertexInputs[2].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
	vertexInputs[3].binding = 3; vertexInputs[3].stride = sizeof(uint32_t) * 4; vertexInputs[3].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
	vertexInputs[4].binding = 4; vertexInputs[4].stride = sizeof(float) * 4; vertexInputs[4].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

	VkVertexInputAttributeDescription vertexAttributes[5]{};
	vertexAttributes[0].binding = 0; vertexAttributes[0].location = 0; vertexAttributes[0].format = VK_FORMAT_R32G32B32_SFLOAT; vertexAttributes[0].offset = 0;
	vertexAttributes[1].binding = 1; vertexAttributes[1].location = 1; vertexAttributes[1].format = VK_FORMAT_R32G32_SFLOAT; vertexAttributes[1].offset = 0;
	vertexAttributes[2].binding = 2; vertexAttributes[2].location = 2; vertexAttributes[2].format = VK_FORMAT_R32G32B32_SFLOAT; vertexAttributes[2].offset = 0;
	vertexAttributes[3].binding = 3; vertexAttributes[3].location = 3; vertexAttributes[3].format = VK_FORMAT_R32G32B32A32_UINT; vertexAttributes[3].offset = 0;
	vertexAttributes[4].binding = 4; vertexAttributes[4].location = 4; vertexAttributes[4].format = VK_FORMAT_R32G32B32A32_SFLOAT; vertexAttributes[4].offset = 0;

	VkPipelineVertexInputStateCreateInfo inputInfo{};
	inputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
	inputInfo.vertexBindingDescriptionCount = 5;
	inputInfo.pVertexBindingDescriptions = vertexInputs;
	inputInfo.vertexAttributeDescriptionCount = 5;
	inputInfo.pVertexAttributeDescriptions = vertexAttributes;

	VkPipelineInputAssemblyStateCreateInfo assemblyInfo{};
	assemblyInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
	assemblyInfo.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
	assemblyInfo.primitiveRestartEnable = VK_FALSE;

	VkViewport viewport{};
	viewport.x = 0.f; viewport.y = 0.f;
	viewport.width = float(aWindow.swapchainExtent.width); viewport.height = float(aWindow.swapchainExtent.height);
	viewport.minDepth = 0.f; viewport.maxDepth = 1.f;

	VkRect2D scissor{};
	scissor.offset = VkOffset2D{ 0, 0 }; scissor.extent = aWindow.swapchainExtent;

	VkPipelineViewportStateCreateInfo viewportInfo{};
	viewportInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
	viewportInfo.viewportCount = 1; viewportInfo.pViewports = &viewport;
	viewportInfo.scissorCount = 1; viewportInfo.pScissors = &scissor;

	VkPipelineRasterizationStateCreateInfo rasterInfo{};
	rasterInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
	rasterInfo.depthClampEnable = VK_FALSE;
	rasterInfo.rasterizerDiscardEnable = VK_FALSE;
	rasterInfo.polygonMode = VK_POLYGON_MODE_FILL;

	// =================================================================
	// 2. 【核心修复】开启背面剔除！不要画出车子内部的黑面！
	// =================================================================
	rasterInfo.cullMode = VK_CULL_MODE_BACK_BIT;

	rasterInfo.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
	rasterInfo.depthBiasEnable = VK_FALSE;
	rasterInfo.lineWidth = 1.f;

	VkPipelineMultisampleStateCreateInfo samplingInfo{};
	samplingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
	samplingInfo.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

	// =================================================================
	// 3. 【核心修复】开启透明混合 (Color Blending)�?
	// =================================================================
	VkPipelineColorBlendAttachmentState blendStates[2]{};
	blendStates[0].blendEnable = VK_TRUE;
	blendStates[0].colorBlendOp = VK_BLEND_OP_ADD;
	blendStates[0].srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
	blendStates[0].dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
	blendStates[0].alphaBlendOp = VK_BLEND_OP_ADD;
	blendStates[0].srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
	blendStates[0].dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
	blendStates[0].colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

	blendStates[1] = blendStates[0]; // 亮度附件也套用一样的混合

	VkPipelineColorBlendStateCreateInfo blendInfo{};
	blendInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
	blendInfo.logicOpEnable = VK_FALSE;
	blendInfo.attachmentCount = 2;
	blendInfo.pAttachments = blendStates;

	// =================================================================
	// 4. 【核心修复】关闭深度写�?(Depth Write)！让它不要挡住后面的单车�?
	// =================================================================
	VkPipelineDepthStencilStateCreateInfo depthInfo{};
	depthInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
	depthInfo.depthTestEnable = VK_TRUE;
	depthInfo.depthWriteEnable = VK_FALSE; // <--- 最重要的一�?
	depthInfo.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
	depthInfo.minDepthBounds = 0.f;
	depthInfo.maxDepthBounds = 1.f;

	VkDynamicState dynamicStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };

	VkPipelineDynamicStateCreateInfo dynamicInfo{};
	dynamicInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
	dynamicInfo.dynamicStateCount = 2;
	dynamicInfo.pDynamicStates = dynamicStates;

	VkFormat colorFormats[] = {
		aColorFormat,
		VK_FORMAT_R16G16B16A16_SFLOAT
	};

	VkPipelineRenderingCreateInfo renderingInfo{};
	renderingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
	renderingInfo.colorAttachmentCount = 2;
	renderingInfo.pColorAttachmentFormats = colorFormats;
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
	if (auto const res = vkCreateGraphicsPipelines(aWindow.device, VK_NULL_HANDLE, 1, &pipeInfo, nullptr, &pipe); VK_SUCCESS != res)
	{
		throw lut::Error("Unable to create graphics pipeline\n"
			"vkCreateGraphicsPipelines() returned {}", lut::to_string(res)
		);
	}

	return lut::Pipeline(aWindow.device, pipe);
}
lut::DescriptorSetLayout create_scene_descriptor_layout( lut::VulkanWindow const& aWindow )
{
	VkDescriptorSetLayoutBinding bindings[3]{};
	bindings[0].binding = 0; // number must match the index of the corresponding binding
	bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	bindings[0].descriptorCount = 1;
	bindings[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

	// p2_1.5 shadow map
	bindings[1].binding = 1;
	bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	bindings[1].descriptorCount = 1;
	bindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

	bindings[2].binding = 2;
	bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	bindings[2].descriptorCount = 1;
	bindings[2].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

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
	VkDescriptorSetLayoutBinding bindings[4]{};
	
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

	// 3: Emissive 
	bindings[3].binding = 3;
	bindings[3].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	bindings[3].descriptorCount = 1;
	bindings[3].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;


	VkDescriptorSetLayoutCreateInfo layoutInfo{};
	layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	layoutInfo.bindingCount = 4;
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
	// bindings for offscreen color and bloom textures
	VkDescriptorSetLayoutBinding bindings[3]{};
	
	bindings[0].binding = 0; 
	bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	bindings[0].descriptorCount = 1;
	bindings[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

	bindings[1].binding = 1;
	bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	bindings[1].descriptorCount = 1;
	bindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

	bindings[2].binding = 2;
	bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	bindings[2].descriptorCount = 1;
	bindings[2].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

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
lut::DescriptorSetLayout create_composite_descriptor_layout(lut::VulkanWindow const& aWindow)
{
	VkDescriptorSetLayoutBinding bindings[3]{};

	// Binding 0: 原始场景纹理 (Main Scene Color)
	bindings[0].binding = 0;
	bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	bindings[0].descriptorCount = 1;
	bindings[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

	// Binding 1: 模糊后的 Bloom 纹理 (Blurred Brightness)
	bindings[1].binding = 1;
	bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	bindings[1].descriptorCount = 1;
	bindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

	// Binding 2: Mosaic Uniform Buffer
	bindings[2].binding = 2;
	bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	bindings[2].descriptorCount = 1;
	bindings[2].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

	VkDescriptorSetLayoutCreateInfo layoutInfo{};
	layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	layoutInfo.bindingCount = 3;
	layoutInfo.pBindings = bindings;

	VkDescriptorSetLayout layout = VK_NULL_HANDLE;
	if (auto const res = vkCreateDescriptorSetLayout(aWindow.device, &layoutInfo, nullptr, &layout); VK_SUCCESS != res)
	{
		throw lut::Error("Unable to create composite descriptor set layout\n"
			"vkCreateDescriptorSetLayout() returned {}", lut::to_string(res)
		);
	}

	return lut::DescriptorSetLayout(aWindow.device, layout);
}

lut::DescriptorSetLayout create_blur_descriptor_layout(lut::VulkanContext const& aContext)
{
	VkDescriptorSetLayoutBinding binding{};
	// Binding 0: 输入纹理（横向模糊时�?Brightness 图，纵向模糊时为 Temp 中间图）
	binding.binding = 0;
	binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	binding.descriptorCount = 1;
	binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

	VkDescriptorSetLayoutCreateInfo layoutInfo{};
	layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	layoutInfo.bindingCount = 1;
	layoutInfo.pBindings = &binding;

	VkDescriptorSetLayout layout = VK_NULL_HANDLE;
	if (auto const res = vkCreateDescriptorSetLayout(aContext.device, &layoutInfo, nullptr, &layout); VK_SUCCESS != res)
	{
		throw lut::Error("Unable to create blur descriptor set layout\n"
			"vkCreateDescriptorSetLayout() returned {}", lut::to_string(res)
		);
	}

	return lut::DescriptorSetLayout(aContext.device, layout);
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
	VkVertexInputBindingDescription vertexInputs[5]{};
	vertexInputs[0].binding = 0; vertexInputs[0].stride = sizeof(float) * 3; vertexInputs[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
	vertexInputs[1].binding = 1; vertexInputs[1].stride = sizeof(float) * 2; vertexInputs[1].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
	vertexInputs[2].binding = 2; vertexInputs[2].stride = sizeof(float) * 3; vertexInputs[2].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
	vertexInputs[3].binding = 3; vertexInputs[3].stride = sizeof(uint32_t) * 4; vertexInputs[3].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
	vertexInputs[4].binding = 4; vertexInputs[4].stride = sizeof(float) * 4; vertexInputs[4].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

	VkVertexInputAttributeDescription vertexAttributes[5]{};
	vertexAttributes[0].binding = 0; vertexAttributes[0].location = 0; vertexAttributes[0].format = VK_FORMAT_R32G32B32_SFLOAT; vertexAttributes[0].offset = 0;
	vertexAttributes[1].binding = 1; vertexAttributes[1].location = 1; vertexAttributes[1].format = VK_FORMAT_R32G32_SFLOAT; vertexAttributes[1].offset = 0;
	vertexAttributes[2].binding = 2; vertexAttributes[2].location = 2; vertexAttributes[2].format = VK_FORMAT_R32G32B32_SFLOAT; vertexAttributes[2].offset = 0;
	vertexAttributes[3].binding = 3; vertexAttributes[3].location = 3; vertexAttributes[3].format = VK_FORMAT_R32G32B32A32_UINT; vertexAttributes[3].offset = 0;
	vertexAttributes[4].binding = 4; vertexAttributes[4].location = 4; vertexAttributes[4].format = VK_FORMAT_R32G32B32A32_SFLOAT; vertexAttributes[4].offset = 0;

	VkPipelineVertexInputStateCreateInfo inputInfo{};
	inputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
	inputInfo.vertexBindingDescriptionCount = 5;
	inputInfo.pVertexBindingDescriptions = vertexInputs;
	inputInfo.vertexAttributeDescriptionCount = 5;
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
	VkPipelineColorBlendAttachmentState blendStates[2]{};
	blendStates[0].blendEnable = VK_FALSE;
	blendStates[0].colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
	
	// Copy exactly identical memory block
	blendStates[1] = blendStates[0];

	VkPipelineColorBlendStateCreateInfo blendInfo{};
	blendInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
	blendInfo.logicOpEnable = VK_FALSE;
	blendInfo.attachmentCount = 2;
	blendInfo.pAttachments = blendStates;

	VkPipelineDepthStencilStateCreateInfo depthInfo{};
	depthInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
	depthInfo.depthTestEnable = VK_TRUE;
	depthInfo.depthWriteEnable = VK_FALSE;
	depthInfo.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
	depthInfo.minDepthBounds = 0.f;
	depthInfo.maxDepthBounds = 1.f;

	VkDynamicState dynamicStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };

	VkPipelineDynamicStateCreateInfo dynamicInfo{};
	dynamicInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
	dynamicInfo.dynamicStateCount = 2;
	dynamicInfo.pDynamicStates = dynamicStates;

	VkFormat colorFormats[] = {
		aColorFormat,                    
		VK_FORMAT_R16G16B16A16_SFLOAT 
	};

	VkPipelineRenderingCreateInfo renderingInfo{};
	renderingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
	renderingInfo.colorAttachmentCount = 2;
	renderingInfo.pColorAttachmentFormats = colorFormats;
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


lut::Pipeline create_blur_pipeline(lut::VulkanWindow const& aWindow, VkPipelineLayout aPipelineLayout) {
	// load shader code
	auto const vertSpirV = lut::load_file_u32(cfg::kFullscreenVertShaderPath);
	auto const fragSpirV = lut::load_file_u32(cfg::kBlurShaderPath);

	VkShaderModuleCreateInfo code[2]{};
	code[0].sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	code[0].codeSize = vertSpirV.size() * sizeof(std::uint32_t);
	code[0].pCode = vertSpirV.data();

	code[1].sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	code[1].codeSize = fragSpirV.size() * sizeof(std::uint32_t);
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


	VkFormat blurFormats[] = { VK_FORMAT_R16G16B16A16_SFLOAT }; // 显式定义数组
	VkPipelineRenderingCreateInfo renderingInfo{};
	renderingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
	renderingInfo.colorAttachmentCount = 1;
	renderingInfo.pColorAttachmentFormats = blurFormats; // 传入数组地址

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
	if (auto const res = vkCreateGraphicsPipelines(aWindow.device, VK_NULL_HANDLE, 1, &pipeInfo, nullptr, &pipe); VK_SUCCESS != res)
	{
		throw lut::Error("Unable to create post proc pipeline\n"
			"vkCreateGraphicsPipelines() returned {}", lut::to_string(res)
		);
	}

	return lut::Pipeline(aWindow.device, pipe);
}

lut::Pipeline create_composite_pipeline(lut::VulkanWindow const& aWindow, VkPipelineLayout aPipelineLayout) {
	// load shader code
	auto const vertSpirV = lut::load_file_u32(cfg::kFullscreenVertShaderPath);
	auto const fragSpirV = lut::load_file_u32(cfg::kComShaderPath);

	VkShaderModuleCreateInfo code[2]{};
	code[0].sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	code[0].codeSize = vertSpirV.size() * sizeof(std::uint32_t);
	code[0].pCode = vertSpirV.data();

	code[1].sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	code[1].codeSize = fragSpirV.size() * sizeof(std::uint32_t);
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
	if (auto const res = vkCreateGraphicsPipelines(aWindow.device, VK_NULL_HANDLE, 1, &pipeInfo, nullptr, &pipe); VK_SUCCESS != res)
	{
		throw lut::Error("Unable to create post proc pipeline\n"
			"vkCreateGraphicsPipelines() returned {}", lut::to_string(res)
		);
	}

	return lut::Pipeline(aWindow.device, pipe);
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

	VkVertexInputBindingDescription vertexInputs[5]{};
	vertexInputs[0].binding = 0; vertexInputs[0].stride = sizeof(float) * 3; vertexInputs[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
	vertexInputs[1].binding = 1; vertexInputs[1].stride = sizeof(float) * 2; vertexInputs[1].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
	vertexInputs[2].binding = 2; vertexInputs[2].stride = sizeof(float) * 3; vertexInputs[2].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
	vertexInputs[3].binding = 3; vertexInputs[3].stride = sizeof(uint32_t) * 4; vertexInputs[3].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
	vertexInputs[4].binding = 4; vertexInputs[4].stride = sizeof(float) * 4; vertexInputs[4].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

	VkVertexInputAttributeDescription vertexAttributes[5]{};
	vertexAttributes[0].binding = 0; vertexAttributes[0].location = 0; vertexAttributes[0].format = VK_FORMAT_R32G32B32_SFLOAT; vertexAttributes[0].offset = 0;
	vertexAttributes[1].binding = 1; vertexAttributes[1].location = 1; vertexAttributes[1].format = VK_FORMAT_R32G32_SFLOAT; vertexAttributes[1].offset = 0;
	vertexAttributes[2].binding = 2; vertexAttributes[2].location = 2; vertexAttributes[2].format = VK_FORMAT_R32G32B32_SFLOAT; vertexAttributes[2].offset = 0;
	vertexAttributes[3].binding = 3; vertexAttributes[3].location = 3; vertexAttributes[3].format = VK_FORMAT_R32G32B32A32_UINT; vertexAttributes[3].offset = 0;
	vertexAttributes[4].binding = 4; vertexAttributes[4].location = 4; vertexAttributes[4].format = VK_FORMAT_R32G32B32A32_SFLOAT; vertexAttributes[4].offset = 0;

	VkPipelineVertexInputStateCreateInfo inputInfo{};
	inputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
	inputInfo.vertexBindingDescriptionCount = 5;
	inputInfo.pVertexBindingDescriptions = vertexInputs;
	inputInfo.vertexAttributeDescriptionCount = 5;
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
	VkPipelineColorBlendAttachmentState blendStates[2]{};
	blendStates[0].blendEnable = VK_TRUE; 
	blendStates[0].colorBlendOp = VK_BLEND_OP_ADD;
	blendStates[0].srcColorBlendFactor = VK_BLEND_FACTOR_ONE; 
	blendStates[0].dstColorBlendFactor = VK_BLEND_FACTOR_ONE; 
	// enable alpha write to keep structural integrity with layout
	blendStates[0].colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
	
	blendStates[1] = blendStates[0];

	VkPipelineColorBlendStateCreateInfo blendInfo{};
	blendInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
	blendInfo.logicOpEnable = VK_FALSE;
	blendInfo.attachmentCount = 2;
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

	VkFormat colorFormats[] = {
		aColorFormat,                    
		VK_FORMAT_R16G16B16A16_SFLOAT 
	};

	VkPipelineRenderingCreateInfo renderingInfo{};
	renderingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
	renderingInfo.colorAttachmentCount = 2;
	renderingInfo.pColorAttachmentFormats = colorFormats;
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

	VkVertexInputBindingDescription vertexInputs[5]{};
	vertexInputs[0].binding = 0; vertexInputs[0].stride = sizeof(float) * 3; vertexInputs[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
	vertexInputs[1].binding = 1; vertexInputs[1].stride = sizeof(float) * 2; vertexInputs[1].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
	vertexInputs[2].binding = 2; vertexInputs[2].stride = sizeof(float) * 3; vertexInputs[2].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
	vertexInputs[3].binding = 3; vertexInputs[3].stride = sizeof(uint32_t) * 4; vertexInputs[3].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
	vertexInputs[4].binding = 4; vertexInputs[4].stride = sizeof(float) * 4; vertexInputs[4].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

	VkVertexInputAttributeDescription vertexAttributes[5]{};
	vertexAttributes[0].binding = 0; vertexAttributes[0].location = 0; vertexAttributes[0].format = VK_FORMAT_R32G32B32_SFLOAT; vertexAttributes[0].offset = 0;
	vertexAttributes[1].binding = 1; vertexAttributes[1].location = 1; vertexAttributes[1].format = VK_FORMAT_R32G32_SFLOAT; vertexAttributes[1].offset = 0;
	vertexAttributes[2].binding = 2; vertexAttributes[2].location = 2; vertexAttributes[2].format = VK_FORMAT_R32G32B32_SFLOAT; vertexAttributes[2].offset = 0;
	vertexAttributes[3].binding = 3; vertexAttributes[3].location = 3; vertexAttributes[3].format = VK_FORMAT_R32G32B32A32_UINT; vertexAttributes[3].offset = 0;
	vertexAttributes[4].binding = 4; vertexAttributes[4].location = 4; vertexAttributes[4].format = VK_FORMAT_R32G32B32A32_SFLOAT; vertexAttributes[4].offset = 0;

	VkPipelineVertexInputStateCreateInfo inputInfo{};
	inputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
	inputInfo.vertexBindingDescriptionCount = 5;
	inputInfo.pVertexBindingDescriptions = vertexInputs;
	inputInfo.vertexAttributeDescriptionCount = 5;
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
	VkPipelineColorBlendAttachmentState blendStates[2]{};
	blendStates[0].blendEnable = VK_TRUE; 
	blendStates[0].colorBlendOp = VK_BLEND_OP_ADD;
	blendStates[0].srcColorBlendFactor = VK_BLEND_FACTOR_ONE; 
	blendStates[0].dstColorBlendFactor = VK_BLEND_FACTOR_ONE; 
	// enable alpha write to keep structural integrity with layout
	blendStates[0].colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
	
	blendStates[1] = blendStates[0];

	VkPipelineColorBlendStateCreateInfo blendInfo{};
	blendInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
	blendInfo.logicOpEnable = VK_FALSE;
	blendInfo.attachmentCount = 2;
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

	VkFormat colorFormats[] = {
		aColorFormat,                    
		VK_FORMAT_R16G16B16A16_SFLOAT 
	};

	VkPipelineRenderingCreateInfo renderingInfo{};
	renderingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
	renderingInfo.colorAttachmentCount = 2;
	renderingInfo.pColorAttachmentFormats = colorFormats;
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

lut::ImageWithView create_shadow_map(lut::VulkanWindow const& aWindow, lut::Allocator const& aAllocator)
{
	// 1. 创建支持多层的图�?
	VkImageCreateInfo imageInfo{};
	imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	imageInfo.imageType = VK_IMAGE_TYPE_2D;
	imageInfo.format = cfg::kShadowMapFormat;
	imageInfo.extent.width = kShadowMapResolution;
	imageInfo.extent.height = kShadowMapResolution;
	imageInfo.extent.depth = 1;
	imageInfo.mipLevels = 1;
	imageInfo.arrayLayers = kCascadeCount; // 核心：设�?4 层级�?
	imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
	imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
	imageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
	imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

	VmaAllocationCreateInfo allocInfo{};
	allocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;

	VkImage image = VK_NULL_HANDLE;
	VmaAllocation allocation = VK_NULL_HANDLE;

	if (auto const res = vmaCreateImage(aAllocator.allocator, &imageInfo, &allocInfo, &image, &allocation, nullptr); VK_SUCCESS != res)
	{
		throw lut::Error("Unable to create shadow map image: {}", lut::to_string(res));
	}

	// 2. 创建�?Array View (给主着色器 default.frag 采样�?
	VkImageViewCreateInfo viewInfo{};
	viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	viewInfo.image = image;
	viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY; // 关键：数组类�?
	viewInfo.format = cfg::kShadowMapFormat;
	viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
	viewInfo.subresourceRange.baseMipLevel = 0;
	viewInfo.subresourceRange.levelCount = 1;
	viewInfo.subresourceRange.baseArrayLayer = 0;
	viewInfo.subresourceRange.layerCount = kCascadeCount; // 覆盖所有级联层

	VkImageView mainView = VK_NULL_HANDLE;
	if (auto const res = vkCreateImageView(aWindow.device, &viewInfo, nullptr, &mainView); VK_SUCCESS != res)
	{
		vmaDestroyImage(aAllocator.allocator, image, allocation);
		throw lut::Error("Unable to create main shadow map view: {}", lut::to_string(res));
	}

	// 提示：你可以在此处手动创建分层视图，或者在 main.cpp 中初始化它们
	// 为了简化，这里返回 ImageWithView，稍后我们在 main.cpp 补充 cascadeViews 的创�?

	return lut::ImageWithView(aAllocator.allocator, image, allocation, mainView);
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

	VkVertexInputBindingDescription vertexInputs[5]{};
	vertexInputs[0].binding = 0; vertexInputs[0].stride = sizeof(float) * 3; vertexInputs[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
	vertexInputs[1].binding = 1; vertexInputs[1].stride = sizeof(float) * 2; vertexInputs[1].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
	vertexInputs[2].binding = 2; vertexInputs[2].stride = sizeof(float) * 3; vertexInputs[2].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
	vertexInputs[3].binding = 3; vertexInputs[3].stride = sizeof(uint32_t) * 4; vertexInputs[3].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
	vertexInputs[4].binding = 4; vertexInputs[4].stride = sizeof(float) * 4; vertexInputs[4].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

	VkVertexInputAttributeDescription vertexAttributes[5]{};
	vertexAttributes[0].binding = 0; vertexAttributes[0].location = 0; vertexAttributes[0].format = VK_FORMAT_R32G32B32_SFLOAT; vertexAttributes[0].offset = 0;
	vertexAttributes[1].binding = 1; vertexAttributes[1].location = 1; vertexAttributes[1].format = VK_FORMAT_R32G32_SFLOAT; vertexAttributes[1].offset = 0;
	vertexAttributes[2].binding = 2; vertexAttributes[2].location = 2; vertexAttributes[2].format = VK_FORMAT_R32G32B32_SFLOAT; vertexAttributes[2].offset = 0;
	vertexAttributes[3].binding = 3; vertexAttributes[3].location = 3; vertexAttributes[3].format = VK_FORMAT_R32G32B32A32_UINT; vertexAttributes[3].offset = 0;
	vertexAttributes[4].binding = 4; vertexAttributes[4].location = 4; vertexAttributes[4].format = VK_FORMAT_R32G32B32A32_SFLOAT; vertexAttributes[4].offset = 0;

	VkPipelineVertexInputStateCreateInfo inputInfo{};
	inputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
	inputInfo.vertexBindingDescriptionCount = 5;
	inputInfo.pVertexBindingDescriptions = vertexInputs;
	inputInfo.vertexAttributeDescriptionCount = 5;
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

//================particle system===================================================
lut::Pipeline create_particle_pipeline(lut::VulkanWindow const& aWindow, VkPipelineLayout aPipelineLayout, VkFormat aColorFormat)
{
	auto const vertSpirV = lut::load_file_u32(cfg::kParticleVertShaderPath);
	auto const fragSpirV = lut::load_file_u32(cfg::kParticleFragShaderPath);

	VkShaderModuleCreateInfo code[2]{};
	code[0].sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	code[0].codeSize = vertSpirV.size() * sizeof(uint32_t);
	code[0].pCode = vertSpirV.data();

	code[1].sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	code[1].codeSize = fragSpirV.size() * sizeof(uint32_t);
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

	VkVertexInputBindingDescription binding{};
	binding.binding = 0;
	binding.stride = sizeof(ParticleVertex);
	binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

	VkVertexInputAttributeDescription attrs[5]{};

	// 1. 位置
	attrs[0].location = 0;
	attrs[0].binding = 0;
	attrs[0].format = VK_FORMAT_R32G32B32_SFLOAT;
	attrs[0].offset = offsetof(ParticleVertex, pos);

	// 2. 大小
	attrs[1].location = 1;
	attrs[1].binding = 0;
	attrs[1].format = VK_FORMAT_R32_SFLOAT;
	attrs[1].offset = offsetof(ParticleVertex, size);

	// 3. 颜色
	attrs[2].location = 2;
	attrs[2].binding = 0;
	attrs[2].format = VK_FORMAT_R32G32B32A32_SFLOAT;
	attrs[2].offset = offsetof(ParticleVertex, color);

	// 4.UV Rect
	attrs[3].location = 3;
	attrs[3].binding = 0;
	attrs[3].format = VK_FORMAT_R32G32B32A32_SFLOAT;
	attrs[3].offset = offsetof(ParticleVertex, uvRect);

	// 5 .旋转
	attrs[4].location = 4;
	attrs[4].binding = 0;
	attrs[4].format = VK_FORMAT_R32_SFLOAT;
	attrs[4].offset = offsetof(ParticleVertex, rotation);

	VkPipelineVertexInputStateCreateInfo vi{};
	vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
	vi.vertexBindingDescriptionCount = 1;
	vi.pVertexBindingDescriptions = &binding;
	vi.vertexAttributeDescriptionCount = 5;
	vi.pVertexAttributeDescriptions = attrs;

	VkPipelineInputAssemblyStateCreateInfo ia{};
	ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
	ia.topology = VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
	ia.primitiveRestartEnable = VK_FALSE;

	VkViewport viewport{};
	VkRect2D scissor{};
	VkPipelineViewportStateCreateInfo vp{};
	vp.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
	vp.viewportCount = 1;
	vp.pViewports = &viewport;
	vp.scissorCount = 1;
	vp.pScissors = &scissor;

	VkPipelineRasterizationStateCreateInfo rs{};
	rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
	rs.polygonMode = VK_POLYGON_MODE_FILL;
	rs.cullMode = VK_CULL_MODE_NONE;
	rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
	rs.lineWidth = 1.f;

	VkPipelineMultisampleStateCreateInfo ms{};
	ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
	ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

	//加法混合Additive Blending
	VkPipelineColorBlendAttachmentState cbAtt[2]{};
	cbAtt[0].blendEnable = VK_TRUE;
	cbAtt[0].srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
	cbAtt[0].dstColorBlendFactor = VK_BLEND_FACTOR_ONE; //  ON为实现色彩相�?
	cbAtt[0].colorBlendOp = VK_BLEND_OP_ADD;
	cbAtt[0].srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
	cbAtt[0].dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
	cbAtt[0].alphaBlendOp = VK_BLEND_OP_ADD;
	cbAtt[0].colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

	// Copy perfectly identical struct for dual attachment compliance
	cbAtt[1] = cbAtt[0];

	VkPipelineColorBlendStateCreateInfo cb{};
	cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
	cb.attachmentCount = 2;
	cb.pAttachments = cbAtt;

	VkPipelineDepthStencilStateCreateInfo ds{};
	ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
	ds.depthTestEnable = VK_TRUE;
	ds.depthWriteEnable = VK_FALSE; // 不写入深度，会有黑边
	ds.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;

	VkDynamicState dynStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
	VkPipelineDynamicStateCreateInfo dyn{};
	dyn.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
	dyn.dynamicStateCount = 2;
	dyn.pDynamicStates = dynStates;

	VkFormat colorFormats[] = {
		aColorFormat,                    
		VK_FORMAT_R16G16B16A16_SFLOAT 
	};

	VkPipelineRenderingCreateInfo renderingInfo{};
	renderingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
	renderingInfo.colorAttachmentCount = 2;
	renderingInfo.pColorAttachmentFormats = colorFormats;
	renderingInfo.depthAttachmentFormat = cfg::kDepthFormat;

	VkGraphicsPipelineCreateInfo pi{};
	pi.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
	pi.pNext = &renderingInfo;
	pi.stageCount = 2;
	pi.pStages = stages;
	pi.pVertexInputState = &vi;
	pi.pInputAssemblyState = &ia;
	pi.pViewportState = &vp;
	pi.pRasterizationState = &rs;
	pi.pMultisampleState = &ms;
	pi.pDepthStencilState = &ds;
	pi.pColorBlendState = &cb;
	pi.pDynamicState = &dyn;
	pi.layout = aPipelineLayout;

	VkPipeline pipe = VK_NULL_HANDLE;
	if (auto const res = vkCreateGraphicsPipelines(aWindow.device, VK_NULL_HANDLE, 1, &pi, nullptr, &pipe);
		VK_SUCCESS != res)
	{
		throw lut::Error("Unable to create particle pipeline\n"
			"vkCreateGraphicsPipelines() returned {}", lut::to_string(res));
	}

	return lut::Pipeline(aWindow.device, pipe);
}

//================debug line===================================================
lut::Pipeline create_debug_line_pipeline(lut::VulkanWindow const& aWindow, VkPipelineLayout aPipelineLayout, VkFormat aColorFormat) {
	auto const vertSpirV = lut::load_file_u32(cfg::kDebugLineVertShaderPath);
	auto const fragSpirV = lut::load_file_u32(cfg::kDebugLineFragShaderPath);

	VkShaderModuleCreateInfo code[2]{};
	code[0].sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	code[0].codeSize = vertSpirV.size() * sizeof(std::uint32_t);
	code[0].pCode = vertSpirV.data();

	code[1].sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	code[1].codeSize = fragSpirV.size() * sizeof(std::uint32_t);
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

	VkVertexInputBindingDescription binding{ 0, sizeof(engine::DebugVertex), VK_VERTEX_INPUT_RATE_VERTEX };

	VkVertexInputAttributeDescription attrs[2]{};
	attrs[0].binding = 0; attrs[0].location = 0; attrs[0].format = VK_FORMAT_R32G32B32_SFLOAT; attrs[0].offset = offsetof(engine::DebugVertex, pos);
	attrs[1].binding = 0; attrs[1].location = 1; attrs[1].format = VK_FORMAT_R32G32B32_SFLOAT; attrs[1].offset = offsetof(engine::DebugVertex, color);

	VkPipelineVertexInputStateCreateInfo inputInfo{};
	inputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
	inputInfo.vertexBindingDescriptionCount = 1;
	inputInfo.pVertexBindingDescriptions = &binding;
	inputInfo.vertexAttributeDescriptionCount = 2;
	inputInfo.pVertexAttributeDescriptions = attrs;

	VkPipelineInputAssemblyStateCreateInfo assemblyInfo{};
	assemblyInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
	assemblyInfo.topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST; 
	assemblyInfo.primitiveRestartEnable = VK_FALSE;

	VkViewport viewport{};
	VkRect2D scissor{};
	VkPipelineViewportStateCreateInfo viewportInfo{};
	viewportInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
	viewportInfo.viewportCount = 1; viewportInfo.pViewports = &viewport;
	viewportInfo.scissorCount = 1; viewportInfo.pScissors = &scissor;

	VkPipelineRasterizationStateCreateInfo rasterInfo{};
	rasterInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
	rasterInfo.polygonMode = VK_POLYGON_MODE_FILL;
	rasterInfo.cullMode = VK_CULL_MODE_NONE; 
	rasterInfo.lineWidth = 1.0f;

	VkPipelineMultisampleStateCreateInfo samplingInfo{};
	samplingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
	samplingInfo.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

	VkPipelineColorBlendAttachmentState blendStates[2]{};
	blendStates[0].blendEnable = VK_FALSE;
	blendStates[0].colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
	blendStates[1] = blendStates[0]; 

	VkPipelineColorBlendStateCreateInfo blendInfo{};
	blendInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
	blendInfo.attachmentCount = 2;
	blendInfo.pAttachments = blendStates;

	VkPipelineDepthStencilStateCreateInfo depthInfo{};
	depthInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
	depthInfo.depthTestEnable = VK_TRUE;
	depthInfo.depthWriteEnable = VK_FALSE;
	depthInfo.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;

	VkDynamicState dynamicStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
	VkPipelineDynamicStateCreateInfo dynamicInfo{};
	dynamicInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
	dynamicInfo.dynamicStateCount = 2;
	dynamicInfo.pDynamicStates = dynamicStates;

	VkFormat colorFormats[] = { aColorFormat, VK_FORMAT_R16G16B16A16_SFLOAT };
	VkPipelineRenderingCreateInfo renderingInfo{};
	renderingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
	renderingInfo.colorAttachmentCount = 2;
	renderingInfo.pColorAttachmentFormats = colorFormats;
	renderingInfo.depthAttachmentFormat = cfg::kDepthFormat;

	VkGraphicsPipelineCreateInfo pipeInfo{};
	pipeInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
	pipeInfo.pNext = &renderingInfo;
	pipeInfo.stageCount = 2; pipeInfo.pStages = stages;
	pipeInfo.pVertexInputState = &inputInfo; pipeInfo.pInputAssemblyState = &assemblyInfo;
	pipeInfo.pViewportState = &viewportInfo; pipeInfo.pRasterizationState = &rasterInfo;
	pipeInfo.pMultisampleState = &samplingInfo; pipeInfo.pDepthStencilState = &depthInfo;
	pipeInfo.pColorBlendState = &blendInfo; pipeInfo.pDynamicState = &dynamicInfo;
	pipeInfo.layout = aPipelineLayout;

	VkPipeline pipe = VK_NULL_HANDLE;
	if (auto const res = vkCreateGraphicsPipelines(aWindow.device, VK_NULL_HANDLE, 1, &pipeInfo, nullptr, &pipe); VK_SUCCESS != res) {
		throw lut::Error("Unable to create debug line pipeline");
	}

	return lut::Pipeline(aWindow.device, pipe);
}


lut::Pipeline create_alpha_pipeline_1_attachment( lut::VulkanWindow const& aWindow, VkPipelineLayout aPipelineLayout, VkFormat aColorFormat )
{
	// Load shader code
	auto const vertSpirV = lut::load_file_u32(cfg::kVertShaderPath); // 改成 kVertShaderPath
	auto const fragSpirV = lut::load_file_u32(cfg::kFragShaderPath); // 改成 kFragShaderPath
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

	VkVertexInputBindingDescription vertexInputs[5]{};
	vertexInputs[0].binding = 0; vertexInputs[0].stride = sizeof(float) * 3; vertexInputs[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
	vertexInputs[1].binding = 1; vertexInputs[1].stride = sizeof(float) * 2; vertexInputs[1].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
	vertexInputs[2].binding = 2; vertexInputs[2].stride = sizeof(float) * 3; vertexInputs[2].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
	vertexInputs[3].binding = 3; vertexInputs[3].stride = sizeof(uint32_t) * 4; vertexInputs[3].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
	vertexInputs[4].binding = 4; vertexInputs[4].stride = sizeof(float) * 4; vertexInputs[4].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

	VkVertexInputAttributeDescription vertexAttributes[5]{};
	vertexAttributes[0].binding = 0; vertexAttributes[0].location = 0; vertexAttributes[0].format = VK_FORMAT_R32G32B32_SFLOAT; vertexAttributes[0].offset = 0;
	vertexAttributes[1].binding = 1; vertexAttributes[1].location = 1; vertexAttributes[1].format = VK_FORMAT_R32G32_SFLOAT; vertexAttributes[1].offset = 0;
	vertexAttributes[2].binding = 2; vertexAttributes[2].location = 2; vertexAttributes[2].format = VK_FORMAT_R32G32B32_SFLOAT; vertexAttributes[2].offset = 0;
	vertexAttributes[3].binding = 3; vertexAttributes[3].location = 3; vertexAttributes[3].format = VK_FORMAT_R32G32B32A32_UINT; vertexAttributes[3].offset = 0;
	vertexAttributes[4].binding = 4; vertexAttributes[4].location = 4; vertexAttributes[4].format = VK_FORMAT_R32G32B32A32_SFLOAT; vertexAttributes[4].offset = 0;

	VkPipelineVertexInputStateCreateInfo inputInfo{};
	inputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
	inputInfo.vertexBindingDescriptionCount = 5;
	inputInfo.pVertexBindingDescriptions = vertexInputs;
	inputInfo.vertexAttributeDescriptionCount = 5;
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

	VkPipelineMultisampleStateCreateInfo samplingInfo{};
	samplingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
	samplingInfo.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

	// Define blend state
	VkPipelineColorBlendAttachmentState blendStates[2]{};
	blendStates[0].blendEnable = VK_FALSE; // masking only, no blending
	blendStates[0].colorBlendOp = VK_BLEND_OP_ADD;
	blendStates[0].srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA; 
	blendStates[0].dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
	blendStates[0].colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

	// Copy exactly identical memory block into index 1 to bypass independentBlend requirement
	blendStates[1] = blendStates[0];

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

	VkFormat colorFormats[] = {
		aColorFormat,                    
		VK_FORMAT_R16G16B16A16_SFLOAT 
	};

	// Pipeline rendering info
	// This is related to dynamic rendering (core in Vulkan 1.3)
	VkPipelineRenderingCreateInfo renderingInfo{};
	renderingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
	renderingInfo.colorAttachmentCount = 1;
	renderingInfo.pColorAttachmentFormats = colorFormats;
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
// ==============================================================================
// 极速特�?(Speed Post-Process) 的管线布局
// ==============================================================================
lut::PipelineLayout create_speed_post_pipeline_layout(lut::VulkanContext const& aContext, VkDescriptorSetLayout aDescriptorLayout)
{
	// 定义 Push Constant 范围，传递一�?4 字节�?float (speedFactor)
	VkPushConstantRange pushConstant{};
	pushConstant.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
	pushConstant.offset = 0;
	pushConstant.size = sizeof(float);

	VkDescriptorSetLayout layouts[] = {
		aDescriptorLayout // set 0: 包含输入渲染好的场景纹理
	};

	VkPipelineLayoutCreateInfo layoutInfo{};
	layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	layoutInfo.setLayoutCount = 1;
	layoutInfo.pSetLayouts = layouts;
	layoutInfo.pushConstantRangeCount = 1;
	layoutInfo.pPushConstantRanges = &pushConstant;

	VkPipelineLayout layout = VK_NULL_HANDLE;
	if (auto const res = vkCreatePipelineLayout(aContext.device, &layoutInfo, nullptr, &layout); VK_SUCCESS != res)
	{
		throw lut::Error("Unable to create speed post proc pipeline layout\n"
			"vkCreatePipelineLayout() returned {}", lut::to_string(res)
		);
	}

	return lut::PipelineLayout(aContext.device, layout);
}

// ==============================================================================
// 极速特�?(Speed Post-Process) 的管�?
// ==============================================================================
lut::Pipeline create_speed_post_pipeline(lut::VulkanWindow const& aWindow, VkPipelineLayout aPipelineLayout)
{
	// 复用全屏顶点着色器，加载新的极速片段着色器
	auto const vertSpirV = lut::load_file_u32(cfg::kFullscreenVertShaderPath);
	auto const fragSpirV = lut::load_file_u32(cfg::kSpeedPostFragShaderPath); // 注意：需要在 cfg 中定义这个路径！

	VkShaderModuleCreateInfo code[2]{};
	code[0].sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	code[0].codeSize = vertSpirV.size() * sizeof(std::uint32_t);
	code[0].pCode = vertSpirV.data();

	code[1].sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	code[1].codeSize = fragSpirV.size() * sizeof(std::uint32_t);
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

	// 没有顶点输入，因为在顶点着色器中通过 gl_VertexIndex 硬编码生成全屏三角形
	VkPipelineVertexInputStateCreateInfo inputInfo{};
	inputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

	VkPipelineInputAssemblyStateCreateInfo assemblyInfo{};
	assemblyInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
	assemblyInfo.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
	assemblyInfo.primitiveRestartEnable = VK_FALSE;

	VkViewport viewport{};
	VkRect2D scissor{};

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
	rasterInfo.cullMode = VK_CULL_MODE_NONE; // 全屏矩形不需要背面剔�?
	rasterInfo.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
	rasterInfo.depthBiasEnable = VK_FALSE;
	rasterInfo.lineWidth = 1.f;

	VkPipelineMultisampleStateCreateInfo samplingInfo{};
	samplingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
	samplingInfo.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

	VkPipelineColorBlendAttachmentState blendStates[1]{};
	blendStates[0].blendEnable = VK_FALSE; // 直接覆盖，不需要混�?
	blendStates[0].colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

	VkPipelineColorBlendStateCreateInfo blendInfo{};
	blendInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
	blendInfo.logicOpEnable = VK_FALSE;
	blendInfo.attachmentCount = 1;
	blendInfo.pAttachments = blendStates;

	// 后期处理不需要深度测�?
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

	// 渲染目标：通常后期处理直接输出�?Swapchain
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
	if (auto const res = vkCreateGraphicsPipelines(aWindow.device, VK_NULL_HANDLE, 1, &pipeInfo, nullptr, &pipe); VK_SUCCESS != res)
	{
		throw lut::Error("Unable to create speed post proc pipeline\n"
			"vkCreateGraphicsPipelines() returned {}", lut::to_string(res)
		);
	}

	return lut::Pipeline(aWindow.device, pipe);
}



// ==============================================================================
// SkyBox 的管线
// ==============================================================================
lut::DescriptorSetLayout create_skybox_descriptor_layout(lut::VulkanWindow const& window) {
	VkDescriptorSetLayoutBinding bindings[2]{};
	bindings[0].binding = 0;
	bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	bindings[0].descriptorCount = 1;
	bindings[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

	bindings[1].binding = 1;
	bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	bindings[1].descriptorCount = 1;
	bindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

	VkDescriptorSetLayoutCreateInfo layoutInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
	layoutInfo.bindingCount = 2; layoutInfo.pBindings = bindings;

	VkDescriptorSetLayout layout = VK_NULL_HANDLE;
	vkCreateDescriptorSetLayout(window.device, &layoutInfo, nullptr, &layout);
	return lut::DescriptorSetLayout(window.device, layout);
}

lut::PipelineLayout create_skybox_pipeline_layout(lut::VulkanWindow const& window, VkDescriptorSetLayout dsetLayout) {
	VkPipelineLayoutCreateInfo layoutInfo{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
	layoutInfo.setLayoutCount = 1; layoutInfo.pSetLayouts = &dsetLayout;

	VkPipelineLayout layout = VK_NULL_HANDLE;
	vkCreatePipelineLayout(window.device, &layoutInfo, nullptr, &layout);
	return lut::PipelineLayout(window.device, layout);
}

lut::Pipeline create_skybox_pipeline(lut::VulkanWindow const& aWindow, VkPipelineLayout aPipelineLayout, VkFormat colorFormat) {
	// 加载 Shader
	auto const vertSpirV = lut::load_file_u32(cfg::skyboxVertShaderPath);
	auto const fragSpirV = lut::load_file_u32(cfg::skyboxFragShaderPath);

	VkShaderModuleCreateInfo code[2]{};
	code[0].sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	code[0].codeSize = vertSpirV.size() * sizeof(std::uint32_t);
	code[0].pCode = vertSpirV.data();

	code[1].sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	code[1].codeSize = fragSpirV.size() * sizeof(std::uint32_t);
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

	// ==========================================================
	// 【关键修复 1】：极简的顶点输入（只有一个 vec3）
	// ==========================================================
	VkVertexInputBindingDescription vertexInputs[1]{};
	vertexInputs[0].binding = 0;
	vertexInputs[0].stride = sizeof(float) * 3; // 只占 3 个 float (x,y,z)
	vertexInputs[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

	VkVertexInputAttributeDescription vertexAttributes[1]{};
	vertexAttributes[0].binding = 0;
	vertexAttributes[0].location = 0;
	vertexAttributes[0].format = VK_FORMAT_R32G32B32_SFLOAT;
	vertexAttributes[0].offset = 0;

	VkPipelineVertexInputStateCreateInfo inputInfo{};
	inputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
	inputInfo.vertexBindingDescriptionCount = 1; // 变成 1
	inputInfo.pVertexBindingDescriptions = vertexInputs;
	inputInfo.vertexAttributeDescriptionCount = 1; // 变成 1
	inputInfo.pVertexAttributeDescriptions = vertexAttributes;

	VkPipelineInputAssemblyStateCreateInfo assemblyInfo{};
	assemblyInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
	assemblyInfo.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
	assemblyInfo.primitiveRestartEnable = VK_FALSE;

	VkViewport viewport{};
	viewport.x = 0.f; viewport.y = 0.f;
	viewport.width = float(aWindow.swapchainExtent.width);
	viewport.height = float(aWindow.swapchainExtent.height);
	viewport.minDepth = 0.f; viewport.maxDepth = 1.f;

	VkRect2D scissor{};
	scissor.offset = VkOffset2D{ 0, 0 };
	scissor.extent = aWindow.swapchainExtent;

	VkPipelineViewportStateCreateInfo viewportInfo{};
	viewportInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
	viewportInfo.viewportCount = 1; viewportInfo.pViewports = &viewport;
	viewportInfo.scissorCount = 1; viewportInfo.pScissors = &scissor;

	// 1. 光栅化状态 (Rasterization)
	VkPipelineRasterizationStateCreateInfo rasterInfo{};
	rasterInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
	rasterInfo.depthClampEnable = VK_FALSE;
	rasterInfo.rasterizerDiscardEnable = VK_FALSE;
	rasterInfo.polygonMode = VK_POLYGON_MODE_FILL;

	// 尝试改成 FRONT_BIT 或 BACK_BIT
	rasterInfo.cullMode = VK_CULL_MODE_BACK_BIT;
	rasterInfo.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
	rasterInfo.depthBiasEnable = VK_FALSE;
	rasterInfo.lineWidth = 1.f;

	VkPipelineMultisampleStateCreateInfo samplingInfo{};
	samplingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
	samplingInfo.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

	// ==========================================================
	// 【关键修复 2】：防止天空盒产生诡异的 Bloom
	// ==========================================================
	VkPipelineColorBlendAttachmentState blendStates[2]{};
	// Attachment 0: 主颜色照常输出天空
	blendStates[0].blendEnable = VK_FALSE;
	blendStates[0].colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

	// Attachment 1: 亮度提取层直接屏蔽写入！(Mask = 0)
	blendStates[1].blendEnable = VK_FALSE;
	blendStates[1].colorWriteMask = 0;

	VkPipelineColorBlendStateCreateInfo blendInfo{};
	blendInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
	blendInfo.logicOpEnable = VK_FALSE;
	blendInfo.attachmentCount = 2;
	blendInfo.pAttachments = blendStates;

	// 让管线自动适应引擎传进来的颜色格式
	VkFormat colorFormats[] = {
		colorFormat, // Location 0: 主颜色 (使用传入的格式)
		colorFormat  // Location 1: Bloom 亮度 (通常与主颜色一致)
	};

	VkPipelineRenderingCreateInfo renderingInfo{};
	renderingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
	renderingInfo.colorAttachmentCount = 2;
	renderingInfo.pColorAttachmentFormats = colorFormats;
	renderingInfo.depthAttachmentFormat = cfg::kDepthFormat;

	// 2. 深度测试状态 (Depth Stencil)
	VkPipelineDepthStencilStateCreateInfo depthInfo{};
	depthInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
	depthInfo.depthTestEnable = VK_TRUE;
	depthInfo.depthWriteEnable = VK_FALSE; // 天空盒绝对不准写入深度
	depthInfo.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL; 
	
	depthInfo.minDepthBounds = 0.f;
	depthInfo.maxDepthBounds = 1.f;

	VkDynamicState dynamicStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
	VkPipelineDynamicStateCreateInfo dynamicInfo{};
	dynamicInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
	dynamicInfo.dynamicStateCount = 2;
	dynamicInfo.pDynamicStates = dynamicStates;

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
	if (auto const res = vkCreateGraphicsPipelines(aWindow.device, VK_NULL_HANDLE, 1, &pipeInfo, nullptr, &pipe); VK_SUCCESS != res)
	{
		throw lut::Error("Unable to create skybox pipeline\n"
			"vkCreateGraphicsPipelines() returned {}", lut::to_string(res)
		);
	}

	return lut::Pipeline(aWindow.device, pipe);
}


// =============================================================================
// Bone-matrices SSBO descriptor layout  (set = 2, binding = 0)
// =============================================================================
lut::DescriptorSetLayout create_bone_descriptor_layout(lut::VulkanContext const& aContext)
{
    VkDescriptorSetLayoutBinding binding{};
    binding.binding         = 0;
    binding.descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    binding.descriptorCount = 1;
    binding.stageFlags      = VK_SHADER_STAGE_VERTEX_BIT;

    VkDescriptorSetLayoutCreateInfo info{};
    info.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    info.bindingCount = 1;
    info.pBindings    = &binding;

    VkDescriptorSetLayout layout = VK_NULL_HANDLE;
    if (auto res = vkCreateDescriptorSetLayout(aContext.device, &info, nullptr, &layout);
        VK_SUCCESS != res)
        throw lut::Error("create_bone_descriptor_layout: {}", lut::to_string(res));

    return lut::DescriptorSetLayout(aContext.device, layout);
}

// =============================================================================
// Skinned pipeline layout: [set0=scene, set1=material, set2=bones] + 128-byte PC
// =============================================================================
lut::PipelineLayout create_skinned_pipeline_layout(
    lut::VulkanContext const& aContext,
    VkDescriptorSetLayout aSceneLayout,
    VkDescriptorSetLayout aObjectLayout,
    VkDescriptorSetLayout aBoneLayout)
{
    VkDescriptorSetLayout layouts[] = { aSceneLayout, aObjectLayout, aBoneLayout };

    VkPushConstantRange pc{};
    pc.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pc.offset     = 0;
    pc.size       = 128;

    VkPipelineLayoutCreateInfo info{};
    info.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    info.setLayoutCount         = 3;
    info.pSetLayouts            = layouts;
    info.pushConstantRangeCount = 1;
    info.pPushConstantRanges    = &pc;

    VkPipelineLayout layout = VK_NULL_HANDLE;
    if (auto res = vkCreatePipelineLayout(aContext.device, &info, nullptr, &layout);
        VK_SUCCESS != res)
        throw lut::Error("create_skinned_pipeline_layout: {}", lut::to_string(res));

    return lut::PipelineLayout(aContext.device, layout);
}

// =============================================================================
// Internal helper: build opaque or alpha-blend skinned pipeline
// =============================================================================
static lut::Pipeline make_skinned_pipeline(
    lut::VulkanWindow const& aWindow,
    VkPipelineLayout aPipelineLayout,
    bool alphaBlend)
{
    auto const vertSpirV = lut::load_file_u32(cfg::kSkinnedVertShaderPath);
    auto const fragSpirV = lut::load_file_u32(cfg::kFragShaderPath);

    VkShaderModuleCreateInfo code[2]{};
    code[0].sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    code[0].codeSize = vertSpirV.size() * sizeof(uint32_t);
    code[0].pCode    = vertSpirV.data();
    code[1].sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    code[1].codeSize = fragSpirV.size() * sizeof(uint32_t);
    code[1].pCode    = fragSpirV.data();

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage  = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].pName  = "main";
    stages[0].pNext  = &code[0];
    stages[1].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].pName  = "main";
    stages[1].pNext  = &code[1];

    // 5 vertex bindings: position, texcoord, normal, joints (uvec4), weights (vec4)
    VkVertexInputBindingDescription bindings[5]{};
    bindings[0] = { 0, sizeof(float)*3,    VK_VERTEX_INPUT_RATE_VERTEX };
    bindings[1] = { 1, sizeof(float)*2,    VK_VERTEX_INPUT_RATE_VERTEX };
    bindings[2] = { 2, sizeof(float)*3,    VK_VERTEX_INPUT_RATE_VERTEX };
    bindings[3] = { 3, sizeof(uint32_t)*4, VK_VERTEX_INPUT_RATE_VERTEX };
    bindings[4] = { 4, sizeof(float)*4,    VK_VERTEX_INPUT_RATE_VERTEX };

    VkVertexInputAttributeDescription attrs[5]{};
    attrs[0] = { 0, 0, VK_FORMAT_R32G32B32_SFLOAT,    0 };
    attrs[1] = { 1, 1, VK_FORMAT_R32G32_SFLOAT,       0 };
    attrs[2] = { 2, 2, VK_FORMAT_R32G32B32_SFLOAT,    0 };
    attrs[3] = { 3, 3, VK_FORMAT_R32G32B32A32_UINT,   0 };
    attrs[4] = { 4, 4, VK_FORMAT_R32G32B32A32_SFLOAT, 0 };

    VkPipelineVertexInputStateCreateInfo inputInfo{};
    inputInfo.sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    inputInfo.vertexBindingDescriptionCount   = 5;
    inputInfo.pVertexBindingDescriptions      = bindings;
    inputInfo.vertexAttributeDescriptionCount = 5;
    inputInfo.pVertexAttributeDescriptions    = attrs;

    VkPipelineInputAssemblyStateCreateInfo assemblyInfo{};
    assemblyInfo.sType    = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    assemblyInfo.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkViewport vp{ 0,0,(float)aWindow.swapchainExtent.width,(float)aWindow.swapchainExtent.height,0,1 };
    VkRect2D sc{ {0,0}, aWindow.swapchainExtent };
    VkPipelineViewportStateCreateInfo vpInfo{};
    vpInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vpInfo.viewportCount = 1; vpInfo.pViewports = &vp;
    vpInfo.scissorCount  = 1; vpInfo.pScissors  = &sc;

    VkPipelineRasterizationStateCreateInfo raster{};
    raster.sType       = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.cullMode    = VK_CULL_MODE_BACK_BIT;
    raster.frontFace   = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    raster.lineWidth   = 1.f;

    VkPipelineMultisampleStateCreateInfo ms{};
    ms.sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState blendAtt[2]{};
    blendAtt[0].colorWriteMask = VK_COLOR_COMPONENT_R_BIT|VK_COLOR_COMPONENT_G_BIT|VK_COLOR_COMPONENT_B_BIT|VK_COLOR_COMPONENT_A_BIT;
    blendAtt[1].colorWriteMask = blendAtt[0].colorWriteMask;
    if (alphaBlend) {
        blendAtt[0].blendEnable         = VK_TRUE;
        blendAtt[0].srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        blendAtt[0].dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        blendAtt[0].colorBlendOp        = VK_BLEND_OP_ADD;
        blendAtt[0].srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        blendAtt[0].dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
        blendAtt[0].alphaBlendOp        = VK_BLEND_OP_ADD;
    }
    VkPipelineColorBlendStateCreateInfo blendInfo{};
    blendInfo.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    blendInfo.attachmentCount = 2;
    blendInfo.pAttachments    = blendAtt;

    VkFormat colorFmts[2] = { VK_FORMAT_R16G16B16A16_SFLOAT, VK_FORMAT_R16G16B16A16_SFLOAT };
    VkPipelineRenderingCreateInfo renderInfo{};
    renderInfo.sType                   = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    renderInfo.colorAttachmentCount    = 2;
    renderInfo.pColorAttachmentFormats = colorFmts;
    renderInfo.depthAttachmentFormat   = cfg::kDepthFormat;

    VkPipelineDepthStencilStateCreateInfo depth{};
    depth.sType            = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depth.depthTestEnable  = VK_TRUE;
    depth.depthWriteEnable = alphaBlend ? VK_FALSE : VK_TRUE;
    depth.depthCompareOp   = VK_COMPARE_OP_LESS_OR_EQUAL;

    VkDynamicState dynS[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dynInfo{};
    dynInfo.sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynInfo.dynamicStateCount = 2;
    dynInfo.pDynamicStates    = dynS;

    VkGraphicsPipelineCreateInfo pipeInfo{};
    pipeInfo.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipeInfo.pNext               = &renderInfo;
    pipeInfo.stageCount          = 2;
    pipeInfo.pStages             = stages;
    pipeInfo.pVertexInputState   = &inputInfo;
    pipeInfo.pInputAssemblyState = &assemblyInfo;
    pipeInfo.pViewportState      = &vpInfo;
    pipeInfo.pRasterizationState = &raster;
    pipeInfo.pMultisampleState   = &ms;
    pipeInfo.pDepthStencilState  = &depth;
    pipeInfo.pColorBlendState    = &blendInfo;
    pipeInfo.pDynamicState       = &dynInfo;
    pipeInfo.layout              = aPipelineLayout;

    VkPipeline pipe = VK_NULL_HANDLE;
    if (auto res = vkCreateGraphicsPipelines(aWindow.device, VK_NULL_HANDLE, 1, &pipeInfo, nullptr, &pipe);
        VK_SUCCESS != res)
        throw lut::Error("make_skinned_pipeline: {}", lut::to_string(res));

    return lut::Pipeline(aWindow.device, pipe);
}

lut::Pipeline create_skinned_pipeline(lut::VulkanWindow const& aWindow,
                                      VkPipelineLayout aPipelineLayout,
                                      VkFormat /*aColorFormat*/)
{
    return make_skinned_pipeline(aWindow, aPipelineLayout, false);
}

lut::Pipeline create_skinned_alpha_pipeline(lut::VulkanWindow const& aWindow,
                                            VkPipelineLayout aPipelineLayout,
                                            VkFormat /*aColorFormat*/)
{
    return make_skinned_pipeline(aWindow, aPipelineLayout, true);
}

