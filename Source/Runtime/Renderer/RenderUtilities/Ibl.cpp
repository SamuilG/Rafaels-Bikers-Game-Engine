#include "Ibl.hpp"
#include "../../Rhi/load.hpp"
#include "../../Rhi/error.hpp"
#include "../../Rhi/to_string.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <filesystem>
#include <vector>

namespace {
namespace lut = labut2;
constexpr VkFormat kFormat = VK_FORMAT_R16G16B16A16_SFLOAT;

void check(VkResult result, const char* operation) {
    if (result != VK_SUCCESS) throw lut::Error("{} failed: {}", operation, lut::to_string(result));
}

lut::ImageWithView makeImage(const lut::VulkanContext& context, const lut::Allocator& allocator,
    uint32_t size, uint32_t levels, bool cube) {
    VkImageCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    info.flags = cube ? VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT : 0;
    info.imageType = VK_IMAGE_TYPE_2D;
    info.format = kFormat;
    info.extent = {size, size, 1};
    info.mipLevels = levels;
    info.arrayLayers = cube ? 6 : 1;
    info.samples = VK_SAMPLE_COUNT_1_BIT;
    info.tiling = VK_IMAGE_TILING_OPTIMAL;
    info.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    VmaAllocationCreateInfo allocationInfo{};
    allocationInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    VkImage image{};
    VmaAllocation allocation{};
    check(vmaCreateImage(allocator.allocator, &info, &allocationInfo, &image, &allocation, nullptr), "IBL image allocation");
    lut::Image owner(allocator.allocator, image, allocation);
    VkImageViewCreateInfo viewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    viewInfo.image = image;
    viewInfo.viewType = cube ? VK_IMAGE_VIEW_TYPE_CUBE : VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = kFormat;
    viewInfo.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, levels, 0, info.arrayLayers};
    VkImageView view{};
    check(vkCreateImageView(context.device, &viewInfo, nullptr, &view), "IBL sampling view");
    return lut::ImageWithView(std::move(owner), view);
}

lut::Pipeline makePipeline(const lut::VulkanContext& context, VkPipelineLayout layout, const char* directory) {
    using Shader = lut::UniqueHandle<VkShaderModule, VkDevice, vkDestroyShaderModule>;
    std::array<Shader, 2> shaders;
    std::array<VkPipelineShaderStageCreateInfo, 2> stages{};
    const char* names[] = {"fullscreen.vert.spv", "ibl_bake.frag.spv"};
    for (size_t i = 0; i < shaders.size(); ++i) {
        auto code = lut::load_file_u32((std::filesystem::path(directory) / names[i]).string().c_str());
        VkShaderModuleCreateInfo info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        info.codeSize = code.size() * sizeof(uint32_t);
        info.pCode = code.data();
        VkShaderModule module{};
        check(vkCreateShaderModule(context.device, &info, nullptr, &module), "IBL shader module");
        shaders[i] = Shader(context.device, module);
        stages[i] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
        stages[i].stage = i == 0 ? VK_SHADER_STAGE_VERTEX_BIT : VK_SHADER_STAGE_FRAGMENT_BIT;
        stages[i].module = module;
        stages[i].pName = "main";
    }
    VkPipelineVertexInputStateCreateInfo vertex{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    VkPipelineInputAssemblyStateCreateInfo assembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo viewport{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    viewport.viewportCount = viewport.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo raster{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.cullMode = VK_CULL_MODE_NONE;
    raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    raster.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo sampling{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    sampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineColorBlendAttachmentState attachment{};
    attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    VkPipelineColorBlendStateCreateInfo blend{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    blend.attachmentCount = 1;
    blend.pAttachments = &attachment;
    const VkDynamicState states[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dynamic.dynamicStateCount = 2;
    dynamic.pDynamicStates = states;
    VkPipelineRenderingCreateInfo rendering{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
    rendering.colorAttachmentCount = 1;
    rendering.pColorAttachmentFormats = &kFormat;
    VkGraphicsPipelineCreateInfo info{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    info.pNext = &rendering;
    info.stageCount = 2;
    info.pStages = stages.data();
    info.pVertexInputState = &vertex;
    info.pInputAssemblyState = &assembly;
    info.pViewportState = &viewport;
    info.pRasterizationState = &raster;
    info.pMultisampleState = &sampling;
    info.pColorBlendState = &blend;
    info.pDynamicState = &dynamic;
    info.layout = layout;
    VkPipeline pipeline{};
    check(vkCreateGraphicsPipelines(context.device, VK_NULL_HANDLE, 1, &info, nullptr, &pipeline), "IBL graphics pipeline");
    return lut::Pipeline(context.device, pipeline);
}

void barrier(VkCommandBuffer command, VkImage image, uint32_t levels, uint32_t layers, bool ready) {
    VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    b.oldLayout = ready ? VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED;
    b.newLayout = ready ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    b.srcAccessMask = ready ? VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT : 0;
    b.dstAccessMask = ready ? VK_ACCESS_SHADER_READ_BIT : VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = image;
    b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, levels, 0, layers};
    vkCmdPipelineBarrier(command, ready ? VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        ready ? VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT : VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, 0, nullptr, 0, nullptr, 1, &b);
}
}

IblResources create_ibl_resources(const labut2::VulkanContext& context, const labut2::Allocator& allocator,
    VkCommandPool commandPool, VkImageView sourceCube, const char* shaderDirectory, const IblBakeSettings& settings) {
    if (!sourceCube || !std::has_single_bit(settings.prefilterSize) || !settings.irradianceSize || !settings.brdfSize ||
        !settings.environmentSamples || !settings.brdfSamples)
        throw labut2::Error("Invalid IBL bake settings or source cubemap");
    IblResources result;
    result.prefilteredMipLevels = std::bit_width(settings.prefilterSize);
    result.prefilteredEnvMap = makeImage(context, allocator, settings.prefilterSize, result.prefilteredMipLevels, true);
    result.irradianceMap = makeImage(context, allocator, settings.irradianceSize, 1, true);
    result.brdfLut = makeImage(context, allocator, settings.brdfSize, 1, false);

    VkSamplerCreateInfo samplerInfo{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    samplerInfo.magFilter = samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.addressModeU = samplerInfo.addressModeV = samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.maxLod = VK_LOD_CLAMP_NONE;
    VkSampler sampler{};
    check(vkCreateSampler(context.device, &samplerInfo, nullptr, &sampler), "IBL sampler");
    result.sampler = labut2::Sampler(context.device, sampler);

    VkDescriptorSetLayoutBinding binding{0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
    VkDescriptorSetLayoutCreateInfo descriptorInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    descriptorInfo.bindingCount = 1;
    descriptorInfo.pBindings = &binding;
    VkDescriptorSetLayout rawLayout{};
    check(vkCreateDescriptorSetLayout(context.device, &descriptorInfo, nullptr, &rawLayout), "IBL descriptor layout");
    labut2::DescriptorSetLayout descriptorLayout(context.device, rawLayout);
    struct BakeParams { int32_t face, mode; float roughness; uint32_t sampleCount; float size; };
    static_assert(sizeof(BakeParams) == 20);
    VkPushConstantRange push{VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(BakeParams)};
    VkPipelineLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &rawLayout;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &push;
    VkPipelineLayout rawPipelineLayout{};
    check(vkCreatePipelineLayout(context.device, &layoutInfo, nullptr, &rawPipelineLayout), "IBL pipeline layout");
    labut2::PipelineLayout pipelineLayout(context.device, rawPipelineLayout);
    auto pipeline = makePipeline(context, rawPipelineLayout, shaderDirectory);
    VkDescriptorPoolSize poolSize{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1};
    VkDescriptorPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    poolInfo.maxSets = 1;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    VkDescriptorPool rawPool{};
    check(vkCreateDescriptorPool(context.device, &poolInfo, nullptr, &rawPool), "IBL descriptor pool");
    labut2::DescriptorPool pool(context.device, rawPool);
    VkDescriptorSetAllocateInfo setInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    setInfo.descriptorPool = rawPool;
    setInfo.descriptorSetCount = 1;
    setInfo.pSetLayouts = &rawLayout;
    VkDescriptorSet set{};
    check(vkAllocateDescriptorSets(context.device, &setInfo, &set), "IBL descriptor set");
    VkDescriptorImageInfo source{sampler, sourceCube, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    write.dstSet = set;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo = &source;
    vkUpdateDescriptorSets(context.device, 1, &write, 0, nullptr);

    // Keep per-face attachment views alive until the submitted bake completes.
    std::vector<labut2::ImageView> faceViews;
    faceViews.reserve(result.prefilteredMipLevels * 6 + 7);
    struct CommandOwner {
        VkDevice device; VkCommandPool pool; VkCommandBuffer command{};
        ~CommandOwner() { if (command) vkFreeCommandBuffers(device, pool, 1, &command); }
    } owner{context.device, commandPool};
    VkCommandBufferAllocateInfo commandInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    commandInfo.commandPool = commandPool;
    commandInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    commandInfo.commandBufferCount = 1;
    check(vkAllocateCommandBuffers(context.device, &commandInfo, &owner.command), "IBL command buffer");
    VkCommandBuffer command = owner.command;
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    check(vkBeginCommandBuffer(command, &begin), "IBL recording");
    vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.handle);
    vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_GRAPHICS, rawPipelineLayout, 0, 1, &set, 0, nullptr);
    auto bake = [&](const labut2::ImageWithView& image, uint32_t size, uint32_t levels, int32_t mode, uint32_t layers, uint32_t samples) {
        barrier(command, image.image, levels, layers, false);
        for (uint32_t mip = 0; mip < levels; ++mip) {
            uint32_t extent = std::max(size >> mip, 1u);
            for (uint32_t face = 0; face < layers; ++face) {
                VkImageViewCreateInfo viewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
                viewInfo.image = image.image;
                viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
                viewInfo.format = kFormat;
                viewInfo.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, mip, 1, face, 1};
                VkImageView view{};
                check(vkCreateImageView(context.device, &viewInfo, nullptr, &view), "IBL attachment view");
                faceViews.emplace_back(context.device, view);
                VkRenderingAttachmentInfo attachment{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
                attachment.imageView = view;
                attachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
                attachment.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
                attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
                VkRenderingInfo rendering{VK_STRUCTURE_TYPE_RENDERING_INFO};
                rendering.renderArea.extent = {extent, extent};
                rendering.layerCount = 1;
                rendering.colorAttachmentCount = 1;
                rendering.pColorAttachments = &attachment;
                vkCmdBeginRendering(command, &rendering);
                VkViewport viewport{0, 0, float(extent), float(extent), 0, 1};
                VkRect2D scissor{{0, 0}, {extent, extent}};
                vkCmdSetViewport(command, 0, 1, &viewport);
                vkCmdSetScissor(command, 0, 1, &scissor);
                BakeParams params{int32_t(face), mode, levels > 1 ? float(mip) / float(levels - 1) : 0.0f, samples, float(extent)};
                vkCmdPushConstants(command, rawPipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(params), &params);
                vkCmdDraw(command, 3, 1, 0, 0);
                vkCmdEndRendering(command);
            }
        }
        barrier(command, image.image, levels, layers, true);
    };
    bake(result.prefilteredEnvMap, settings.prefilterSize, result.prefilteredMipLevels, 0, 6, settings.environmentSamples);
    bake(result.irradianceMap, settings.irradianceSize, 1, 1, 6, settings.environmentSamples);
    bake(result.brdfLut, settings.brdfSize, 1, 2, 1, settings.brdfSamples);
    check(vkEndCommandBuffer(command), "IBL recording end");
    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &command;
    check(vkQueueSubmit(context.graphicsQueue, 1, &submit, VK_NULL_HANDLE), "IBL submission");
    check(vkQueueWaitIdle(context.graphicsQueue), "IBL completion");
    return result;
}
