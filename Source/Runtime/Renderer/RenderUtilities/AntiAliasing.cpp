#include "AntiAliasing.hpp"
#include "../../Rhi/load.hpp"
#include "../../Rhi/error.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>

namespace {
namespace lut = labut2;
void check(VkResult r, const char* op) { if (r != VK_SUCCESS) throw lut::Error("{} failed ({})", op, int(r)); }
lut::ImageWithView image(const lut::VulkanContext& context, const lut::Allocator& allocator,
    VkExtent2D extent, VkFormat format, VkSampleCountFlagBits samples, VkImageUsageFlags usage) {
    VkImageCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    info.imageType = VK_IMAGE_TYPE_2D; info.format = format;
    info.extent = {extent.width, extent.height, 1}; info.mipLevels = info.arrayLayers = 1;
    info.samples = samples; info.tiling = VK_IMAGE_TILING_OPTIMAL; info.usage = usage;
    VmaAllocationCreateInfo ai{}; ai.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    VkImage handle{}; VmaAllocation allocation{};
    check(vmaCreateImage(allocator.allocator, &info, &ai, &handle, &allocation, nullptr), "AA image");
    lut::Image owner(allocator.allocator, handle, allocation);
    VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    vi.image = handle; vi.viewType = VK_IMAGE_VIEW_TYPE_2D; vi.format = format;
    vi.subresourceRange = {VkImageAspectFlags(format == VK_FORMAT_D32_SFLOAT ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT), 0, 1, 0, 1};
    VkImageView view{}; check(vkCreateImageView(context.device, &vi, nullptr, &view), "AA image view");
    return lut::ImageWithView(std::move(owner), view);
}
void barrier(VkCommandBuffer cmd, VkImage image, VkImageLayout before, VkImageLayout after,
    VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT) {
    VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    b.srcAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
    b.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
    b.oldLayout = before; b.newLayout = after; b.image = image;
    b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.subresourceRange = {aspect, 0, 1, 0, 1};
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
        0, 0, nullptr, 0, nullptr, 1, &b);
}
lut::DescriptorSetLayout setLayout(VkDevice device, const std::vector<VkDescriptorSetLayoutBinding>& bindings) {
    VkDescriptorSetLayoutCreateInfo info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    info.bindingCount = uint32_t(bindings.size()); info.pBindings = bindings.data();
    VkDescriptorSetLayout result{}; check(vkCreateDescriptorSetLayout(device, &info, nullptr, &result), "AA descriptor layout");
    return lut::DescriptorSetLayout(device, result);
}
lut::PipelineLayout pipeLayout(VkDevice device, VkDescriptorSetLayout set) {
    VkPipelineLayoutCreateInfo info{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    info.setLayoutCount = 1; info.pSetLayouts = &set;
    VkPipelineLayout result{}; check(vkCreatePipelineLayout(device, &info, nullptr, &result), "AA pipeline layout");
    return lut::PipelineLayout(device, result);
}
lut::Pipeline pipeline(VkDevice device, VkPipelineLayout layout, const char* directory, const char* vert,
    const char* frag, bool motion, bool skinned) {
    using Shader = lut::UniqueHandle<VkShaderModule, VkDevice, vkDestroyShaderModule>;
    std::array<Shader, 2> modules;
    VkPipelineShaderStageCreateInfo stages[2]{};
    const char* names[]{vert, frag};
    for (uint32_t i = 0; i < 2; ++i) {
        auto code = lut::load_file_u32((std::filesystem::path(directory) / names[i]).string().c_str());
        VkShaderModuleCreateInfo info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        info.codeSize = code.size() * sizeof(uint32_t); info.pCode = code.data();
        VkShaderModule module{}; check(vkCreateShaderModule(device, &info, nullptr, &module), "AA shader");
        modules[i] = Shader(device, module);
        stages[i] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
        stages[i].stage = i ? VK_SHADER_STAGE_FRAGMENT_BIT : VK_SHADER_STAGE_VERTEX_BIT;
        stages[i].module = module; stages[i].pName = "main";
    }
    VkVertexInputBindingDescription bindings[]{{0, sizeof(glm::vec3), VK_VERTEX_INPUT_RATE_VERTEX},
        {1, sizeof(glm::uvec4), VK_VERTEX_INPUT_RATE_VERTEX}, {2, sizeof(glm::vec4), VK_VERTEX_INPUT_RATE_VERTEX}};
    VkVertexInputAttributeDescription attributes[]{{0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0},
        {1, 1, VK_FORMAT_R32G32B32A32_UINT, 0}, {2, 2, VK_FORMAT_R32G32B32A32_SFLOAT, 0}};
    VkPipelineVertexInputStateCreateInfo vertex{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    if (motion) {
        vertex.vertexBindingDescriptionCount = vertex.vertexAttributeDescriptionCount = skinned ? 3 : 1;
        vertex.pVertexBindingDescriptions = bindings; vertex.pVertexAttributeDescriptions = attributes;
    }
    VkPipelineInputAssemblyStateCreateInfo assembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo viewport{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    viewport.viewportCount = viewport.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo raster{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    raster.polygonMode = VK_POLYGON_MODE_FILL; raster.cullMode = VK_CULL_MODE_NONE; raster.lineWidth = 1;
    VkPipelineMultisampleStateCreateInfo samples{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    samples.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineColorBlendAttachmentState blend{}; blend.colorWriteMask = 15;
    VkPipelineColorBlendStateCreateInfo blending{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    blending.attachmentCount = 1; blending.pAttachments = &blend;
    const VkDynamicState states[]{VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dynamic.dynamicStateCount = 2; dynamic.pDynamicStates = states;
    const VkFormat format = VK_FORMAT_R16G16B16A16_SFLOAT;
    VkPipelineRenderingCreateInfo rendering{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
    rendering.colorAttachmentCount = 1; rendering.pColorAttachmentFormats = &format;
    VkGraphicsPipelineCreateInfo info{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    info.pNext = &rendering; info.layout = layout; info.stageCount = 2; info.pStages = stages;
    info.pVertexInputState = &vertex; info.pInputAssemblyState = &assembly; info.pViewportState = &viewport;
    info.pRasterizationState = &raster; info.pMultisampleState = &samples;
    info.pColorBlendState = &blending; info.pDynamicState = &dynamic;
    VkPipeline result{}; check(vkCreateGraphicsPipelines(device, {}, 1, &info, nullptr, &result), "AA pipeline");
    return lut::Pipeline(device, result);
}
void upload(const lut::Allocator& allocator, lut::Buffer& buffer, const void* data, size_t size) {
    if (!size) return;
    void* mapped{}; check(vmaMapMemory(allocator.allocator, buffer.allocation, &mapped), "AA buffer map");
    std::memcpy(mapped, data, size);
    check(vmaFlushAllocation(allocator.allocator, buffer.allocation, 0, size), "AA buffer flush");
    vmaUnmapMemory(allocator.allocator, buffer.allocation);
}
}

namespace engine {
MsaaSupport QueryMsaaSupport(VkPhysicalDevice device) {
    VkPhysicalDeviceDepthStencilResolveProperties resolve{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEPTH_STENCIL_RESOLVE_PROPERTIES};
    VkPhysicalDeviceProperties2 props{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2}; props.pNext = &resolve;
    vkGetPhysicalDeviceProperties2(device, &props);
    MsaaSupport result;
    if (resolve.supportedDepthResolveModes & VK_RESOLVE_MODE_MIN_BIT) result.depthResolve = VK_RESOLVE_MODE_MIN_BIT;
    else if (resolve.supportedDepthResolveModes & VK_RESOLVE_MODE_SAMPLE_ZERO_BIT) result.depthResolve = VK_RESOLVE_MODE_SAMPLE_ZERO_BIT;
    if (result.depthResolve == VK_RESOLVE_MODE_NONE) return result;
    uint32_t supported = props.properties.limits.framebufferColorSampleCounts & props.properties.limits.framebufferDepthSampleCounts;
    for (auto format : {VK_FORMAT_R16G16B16A16_SFLOAT, VK_FORMAT_D32_SFLOAT}) {
        VkImageFormatProperties imageProps{};
        auto usage = format == VK_FORMAT_D32_SFLOAT ? VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT : VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        if (vkGetPhysicalDeviceImageFormatProperties(device, format, VK_IMAGE_TYPE_2D, VK_IMAGE_TILING_OPTIMAL,
            usage, 0, &imageProps) != VK_SUCCESS) return result;
        supported &= imageProps.sampleCounts;
    }
    result.sampleCounts = 1 | (supported & (2 | 4 | 8));
    return result;
}
uint32_t SelectMsaaSamples(uint32_t requested, uint32_t supported) {
    for (uint32_t count : {8u, 4u, 2u, 1u}) if (count <= requested && (count & supported)) return count;
    return 1;
}
glm::vec2 TemporalJitter(uint32_t frame) {
    auto halton = [](uint32_t n, uint32_t base) {
        float value = 0, fraction = 1;
        while (n) { fraction /= float(base); value += fraction * float(n % base); n /= base; }
        return value;
    };
    const uint32_t n = frame % 16 + 1;
    return {halton(n, 2) - 0.5f, halton(n, 3) - 0.5f};
}
void MsaaTargets::Configure(const lut::VulkanContext& context, const lut::Allocator& allocator,
    VkExtent2D extent, uint32_t count, VkResolveModeFlagBits mode) {
    for (auto& color : colors) color = {};
    depth = {}; samples = VkSampleCountFlagBits(count); depthResolve = mode;
    if (count == 1) return;
    for (auto& color : colors) color = image(context, allocator, extent, VK_FORMAT_R16G16B16A16_SFLOAT,
        samples, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT);
    depth = image(context, allocator, extent, VK_FORMAT_D32_SFLOAT, samples, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT);
}
void MsaaTargets::Attach(VkCommandBuffer cmd, VkRenderingAttachmentInfo* attachments, VkRenderingAttachmentInfo& depthAttachment) const {
    if (samples == VK_SAMPLE_COUNT_1_BIT) return;
    for (size_t i = 0; i < colors.size(); ++i) {
        barrier(cmd, colors[i].image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        auto& a = attachments[i];
        a.resolveMode = VK_RESOLVE_MODE_AVERAGE_BIT; a.resolveImageView = a.imageView;
        a.resolveImageLayout = a.imageLayout; a.imageView = colors[i].view;
        a.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    }
    barrier(cmd, depth.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, VK_IMAGE_ASPECT_DEPTH_BIT);
    depthAttachment.resolveMode = depthResolve; depthAttachment.resolveImageView = depthAttachment.imageView;
    depthAttachment.resolveImageLayout = depthAttachment.imageLayout; depthAttachment.imageView = depth.view;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
}
void TemporalAntiAliasing::Initialize(const lut::VulkanContext& context, const char* directory) {
    static_assert(sizeof(Uniform) == 304 && sizeof(Object) == 144);
    const auto both = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    mMotionSetLayout = setLayout(context.device, {{0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, both, nullptr},
        {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT, nullptr},
        {2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT, nullptr},
        {3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr}});
    std::vector<VkDescriptorSetLayoutBinding> bindings;
    for (uint32_t i = 0; i < 4; ++i) bindings.push_back({i, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr});
    bindings.push_back({4, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr});
    mResolveSetLayout = setLayout(context.device, bindings);
    mMotionLayout = pipeLayout(context.device, mMotionSetLayout.handle);
    mResolveLayout = pipeLayout(context.device, mResolveSetLayout.handle);
    mMotionPipe = pipeline(context.device, mMotionLayout.handle, directory, "taa_motion.vert.spv", "taa_motion.frag.spv", true, false);
    mSkinnedMotionPipe = pipeline(context.device, mMotionLayout.handle, directory, "taa_motion_skinned.vert.spv", "taa_motion.frag.spv", true, true);
    mResolvePipe = pipeline(context.device, mResolveLayout.handle, directory, "fullscreen.vert.spv", "taa.frag.spv", false, false);
    VkSamplerCreateInfo info{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    info.minFilter = info.magFilter = VK_FILTER_LINEAR;
    info.addressModeU = info.addressModeV = info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    VkSampler sampler{}; check(vkCreateSampler(context.device, &info, nullptr, &sampler), "TAA sampler");
    mSampler = lut::Sampler(context.device, sampler);
    info.minFilter = info.magFilter = VK_FILTER_NEAREST;
    check(vkCreateSampler(context.device, &info, nullptr, &sampler), "TAA depth sampler");
    mDepthSampler = lut::Sampler(context.device, sampler);
}
void TemporalAntiAliasing::Resize(const lut::VulkanContext& context, const lut::Allocator& allocator, VkExtent2D extent, size_t slots) {
    mFrames.clear(); mPool = {}; mExtent = extent;
    const auto usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    for (auto& history : mHistory) history = image(context, allocator, extent, VK_FORMAT_R16G16B16A16_SFLOAT, VK_SAMPLE_COUNT_1_BIT, usage);
    mMotion = image(context, allocator, extent, VK_FORMAT_R16G16B16A16_SFLOAT, VK_SAMPLE_COUNT_1_BIT,
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
    VkDescriptorPoolSize sizes[]{{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, uint32_t(slots * 9)},
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, uint32_t(slots * 3)}, {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, uint32_t(slots * 2)}};
    VkDescriptorPoolCreateInfo info{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    info.maxSets = uint32_t(slots * 3); info.poolSizeCount = 3; info.pPoolSizes = sizes;
    VkDescriptorPool pool{}; check(vkCreateDescriptorPool(context.device, &info, nullptr, &pool), "TAA descriptor pool");
    mPool = lut::DescriptorPool(context.device, pool); mFrames.resize(slots);
    for (auto& frame : mFrames) {
        frame.uniform = lut::create_buffer(allocator, sizeof(Uniform), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT);
        VkDescriptorSetLayout layouts[]{mMotionSetLayout.handle, mResolveSetLayout.handle, mResolveSetLayout.handle};
        VkDescriptorSetAllocateInfo allocate{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        allocate.descriptorPool = pool; allocate.descriptorSetCount = 3; allocate.pSetLayouts = layouts;
        VkDescriptorSet sets[3]; check(vkAllocateDescriptorSets(context.device, &allocate, sets), "TAA descriptors");
        frame.motionSet = sets[0]; frame.resolveSets[0] = sets[1]; frame.resolveSets[1] = sets[2];
    }
    mHistoryInitialized = {}; mReadHistory = 0; ResetHistory();
}
void TemporalAntiAliasing::ResetHistory() {
    mHistoryValid = mHaveCamera = false; mFrameNumber = 0; mJitter = {};
    mPreviousObjects.clear(); mPreviousBones.clear();
}
void TemporalAntiAliasing::Shutdown() {
    mFrames.clear(); mPool = {}; mMotion = {};
    for (auto& history : mHistory) history = {};
    mMotionPipe = {}; mSkinnedMotionPipe = {}; mResolvePipe = {};
    mMotionLayout = {}; mResolveLayout = {}; mMotionSetLayout = {}; mResolveSetLayout = {};
    mSampler = {}; mDepthSampler = {}; ResetHistory();
}
bool TemporalAntiAliasing::BeginFrame(const RenderSettings& settings, const glm::mat4& view, const glm::mat4& projection,
    glm::vec2 viewport, const void* scene, bool enabled) {
    bool cut = mEnabled != enabled || scene != mLastScene || !(settings == mLastSettings) || viewport != mLastViewport;
    if (mHaveCamera) {
        const glm::mat4 world = glm::inverse(view), lastWorld = glm::inverse(mLastView);
        cut |= glm::length(glm::vec3(world[3] - lastWorld[3])) > 5.0f;
        cut |= glm::dot(glm::normalize(glm::vec3(world[2])), glm::normalize(glm::vec3(lastWorld[2]))) < 0.7f;
        // Previous VP reprojection also handles smooth gameplay FOV changes.
        // Reset only for a projection cut, rather than every auto-FOV frame.
        for (int column = 0; column < 4; ++column)
            for (int row = 0; row < 4; ++row)
                cut |= std::abs(projection[column][row] - mLastProjection[column][row]) >
                    0.15f * std::max(std::abs(mLastProjection[column][row]), 0.1f);
    }
    if (cut || !enabled) ResetHistory();
    mEnabled = enabled; mLastScene = scene; mLastSettings = settings;
    mLastView = view; mLastProjection = projection; mLastViewport = viewport; mHaveCamera = enabled;
    mJitter = enabled ? TemporalJitter(mFrameNumber) : glm::vec2(0);
    return enabled;
}
void TemporalAntiAliasing::Prepare(const lut::VulkanContext& context, const lut::Allocator& allocator, size_t slot,
    const glm::mat4& vp, const glm::mat4& projection, const std::vector<RenderBatch>& opaque,
    const std::vector<RenderBatch>& skinned, const std::vector<glm::mat4>& bones, VkImageView color, VkImageView depth) {
    auto& frame = mFrames.at(slot); frame.draws.clear(); mPendingObjects.clear();
    std::vector<Object> objects;
    std::vector<glm::mat4> packedBones = bones;
    const uint32_t previousBase = uint32_t(packedBones.size());
    packedBones.insert(packedBones.end(), mPreviousBones.begin(), mPreviousBones.end());
    if (packedBones.empty()) packedBones.push_back(glm::mat4(1));
    auto append = [&](const auto& batches, bool skin) {
        for (const auto& batch : batches) {
            const Key key{batch.entityId, batch.meshIndex, batch.isPortalClone};
            auto old = mPreviousObjects.find(key);
            bool valid = mHistoryValid && batch.entityId != 0 && old != mPreviousObjects.end();
            if (skin && batch.boneBaseIndex >= bones.size()) continue;
            // Copies created for portal crossings are reactive; the moving clip
            // boundary and dither coverage must not reuse another surface's history.
            valid &= !batch.isPortalClone && batch.ditherFade > 0.999f && glm::dot(batch.clipPlane, batch.clipPlane) < 0.0001f;
            if (skin && valid) valid &= old->second.boneBase < mPreviousBones.size();
            Object object{batch.transform, valid ? old->second.model : batch.transform,
                {batch.boneBaseIndex, valid && skin ? previousBase + old->second.boneBase : batch.boneBaseIndex, skin ? 1u : 0u, valid ? 1u : 0u}};
            frame.draws.push_back({batch.meshIndex, uint32_t(objects.size()), skin}); objects.push_back(object);
            mPendingObjects[key] = {batch.transform, batch.boneBaseIndex};
        }
    };
    append(opaque, false); append(skinned, true);
    if (objects.empty()) objects.push_back({});
    auto ensure = [&](lut::Buffer& buffer, size_t& capacity, size_t bytes) {
        if (capacity >= bytes) return;
        capacity = std::max(bytes, capacity * 2);
        buffer = lut::create_buffer(allocator, capacity, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT);
    };
    ensure(frame.objects, frame.objectCapacity, objects.size() * sizeof(Object));
    ensure(frame.bones, frame.boneCapacity, packedBones.size() * sizeof(glm::mat4));
    Uniform uniform{vp, mHistoryValid ? mPreviousViewProjection : vp, glm::inverse(projection),
        {std::clamp(mLastSettings.taaHistoryWeight, 0.0f, 0.98f), mHistoryValid ? 1.0f : 0.0f, float(mExtent.width), float(mExtent.height)},
        glm::inverse(vp), glm::inverse(mLastView)[3], mPreviousCameraPosition};
    upload(allocator, frame.uniform, &uniform, sizeof(uniform));
    upload(allocator, frame.objects, objects.data(), objects.size() * sizeof(Object));
    upload(allocator, frame.bones, packedBones.data(), packedBones.size() * sizeof(glm::mat4));
    VkDescriptorBufferInfo buffers[]{{frame.uniform.buffer, 0, sizeof(Uniform)},
        {frame.objects.buffer, 0, frame.objectCapacity}, {frame.bones.buffer, 0, frame.boneCapacity}};
    std::vector<VkWriteDescriptorSet> writes;
    for (uint32_t i = 0; i < 3; ++i) {
        VkWriteDescriptorSet w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        w.dstSet = frame.motionSet; w.dstBinding = i; w.descriptorCount = 1;
        w.descriptorType = i ? VK_DESCRIPTOR_TYPE_STORAGE_BUFFER : VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        w.pBufferInfo = &buffers[i]; writes.push_back(w);
    }
    VkDescriptorImageInfo depthInfo{mDepthSampler.handle, depth, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL};
    VkWriteDescriptorSet w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    w.dstSet = frame.motionSet; w.dstBinding = 3; w.descriptorCount = 1;
    w.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; w.pImageInfo = &depthInfo; writes.push_back(w);
    vkUpdateDescriptorSets(context.device, uint32_t(writes.size()), writes.data(), 0, nullptr);
    for (uint32_t history = 0; history < 2; ++history) {
        VkDescriptorImageInfo images[]{{mSampler.handle, color, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
            {mSampler.handle, mHistory[history].view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}, depthInfo,
            {mDepthSampler.handle, mMotion.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}};
        writes.clear();
        for (uint32_t i = 0; i < 5; ++i) {
            VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
            write.dstSet = frame.resolveSets[history]; write.dstBinding = i; write.descriptorCount = 1;
            write.descriptorType = i < 4 ? VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER : VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            if (i < 4) write.pImageInfo = &images[i]; else write.pBufferInfo = &buffers[0];
            writes.push_back(write);
        }
        vkUpdateDescriptorSets(context.device, uint32_t(writes.size()), writes.data(), 0, nullptr);
    }
    mPendingViewProjection = vp; mPendingBones = bones;
}
void TemporalAntiAliasing::Resolve(VkCommandBuffer cmd, size_t slot, VkImage color, const TemporalGeometry& geometry) {
    if (!mEnabled) return;
    auto& frame = mFrames.at(slot);
    barrier(cmd, mMotion.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    VkRenderingAttachmentInfo attachment{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    attachment.imageView = mMotion.view; attachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR; attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    VkRenderingInfo pass{VK_STRUCTURE_TYPE_RENDERING_INFO};
    pass.renderArea.extent = mExtent; pass.layerCount = pass.colorAttachmentCount = 1; pass.pColorAttachments = &attachment;
    const VkViewport viewport{0, 0, float(mExtent.width), float(mExtent.height), 0, 1};
    const VkRect2D scissor{{0, 0}, mExtent};
    vkCmdBeginRendering(cmd, &pass); vkCmdSetViewport(cmd, 0, 1, &viewport); vkCmdSetScissor(cmd, 0, 1, &scissor);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, mMotionLayout.handle, 0, 1, &frame.motionSet, 0, nullptr);
    for (const auto& draw : frame.draws) {
        if (draw.mesh >= geometry.positions.size() || draw.mesh >= geometry.indices.size() || draw.mesh >= geometry.indexCounts.size()) continue;
        const VkDeviceSize zero = 0;
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, draw.skinned ? mSkinnedMotionPipe.handle : mMotionPipe.handle);
        vkCmdBindVertexBuffers(cmd, 0, 1, &geometry.positions[draw.mesh].buffer, &zero);
        if (draw.skinned) {
            const auto j = geometry.joints.find(draw.mesh), w = geometry.weights.find(draw.mesh);
            if (j == geometry.joints.end() || w == geometry.weights.end()) continue;
            vkCmdBindVertexBuffers(cmd, 1, 1, &j->second.buffer, &zero); vkCmdBindVertexBuffers(cmd, 2, 1, &w->second.buffer, &zero);
        }
        vkCmdBindIndexBuffer(cmd, geometry.indices[draw.mesh].buffer, 0, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(cmd, geometry.indexCounts[draw.mesh], 1, 0, 0, draw.object);
    }
    vkCmdEndRendering(cmd);
    barrier(cmd, mMotion.image, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    for (uint32_t i = 0; i < 2; ++i) if (!mHistoryInitialized[i]) {
        barrier(cmd, mHistory[i].image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        const VkClearColorValue clear{}; const VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        vkCmdClearColorImage(cmd, mHistory[i].image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clear, 1, &range);
        barrier(cmd, mHistory[i].image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        mHistoryInitialized[i] = true;
    }
    barrier(cmd, color, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    const uint32_t write = 1 - mReadHistory;
    barrier(cmd, mHistory[write].image, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    attachment.imageView = mHistory[write].view; attachment.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    vkCmdBeginRendering(cmd, &pass); vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, mResolvePipe.handle);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, mResolveLayout.handle, 0, 1, &frame.resolveSets[mReadHistory], 0, nullptr);
    vkCmdDraw(cmd, 3, 1, 0, 0); vkCmdEndRendering(cmd);
    barrier(cmd, mHistory[write].image, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    barrier(cmd, color, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    VkImageCopy copy{}; copy.srcSubresource = copy.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    copy.extent = {mExtent.width, mExtent.height, 1};
    vkCmdCopyImage(cmd, mHistory[write].image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, color, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
    barrier(cmd, mHistory[write].image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    barrier(cmd, color, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
}
void TemporalAntiAliasing::Submitted() {
    if (!mEnabled) return;
    mReadHistory = 1 - mReadHistory; mHistoryValid = true; ++mFrameNumber;
    mPreviousObjects = std::move(mPendingObjects); mPreviousBones = std::move(mPendingBones);
    mPreviousViewProjection = mPendingViewProjection;
    mPreviousCameraPosition = glm::inverse(mLastView)[3];
}
} // namespace engine
