// Headless tests execute the production IBL baker and PBR/composite shaders.
#include "Runtime/Renderer/RenderUtilities/Ibl.hpp"
#include "Runtime/Renderer/RenderUtilities/SkyboxAttachments.hpp"
#include "Runtime/Renderer/RenderUtilities/RenderParameters.hpp"
#include "Runtime/Rhi/vkbuffer.hpp"
#include "Runtime/Rhi/load.hpp"
#include <glm/glm.hpp>
#include <stb_image.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

// Own the headless instance/device without the application's window setup.
namespace labut2 {
VulkanContext::VulkanContext() = default;
VulkanContext::~VulkanContext() = default;
}
namespace {
using Pixel = std::array<float, 4>;
void Require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
void Check(VkResult result, const char* operation) {
    if (result != VK_SUCCESS) throw std::runtime_error(std::string(operation) + ": " + std::to_string(result));
}
#define VK_CHECK(expression) Check((expression), #expression)
float Half(uint16_t bits) {
    const float sign = bits & 0x8000 ? -1.f : 1.f;
    const unsigned exponent = (bits >> 10) & 31, mantissa = bits & 1023;
    if (exponent == 31) return mantissa ? NAN : sign * INFINITY;
    return sign * (exponent ? std::ldexp(1.f + mantissa / 1024.f, int(exponent) - 15)
                           : std::ldexp(mantissa / 1024.f, -14));
}
class Gpu {
public:
    labut2::VulkanContext context;
    labut2::Allocator allocator;
    VkCommandPool pool{};
    VkCommandBuffer command{};
    std::string shaders;
    std::vector<labut2::Buffer> buffers;
    std::vector<labut2::ImageWithView> images;
    static inline uint32_t validationErrors = 0;
    VkDebugUtilsMessengerEXT messenger{};
    static VKAPI_ATTR VkBool32 VKAPI_CALL Debug(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
        VkDebugUtilsMessageTypeFlagsEXT, const VkDebugUtilsMessengerCallbackDataEXT* data, void*) {
        if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
            ++validationErrors; std::cerr << "Validation: " << data->pMessage << '\n';
        }
        return VK_FALSE;
    }
    explicit Gpu(const char* directory) : shaders(directory) {
        VK_CHECK(volkInitialize());
        VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
        app.pApplicationName = "Steer PBR GPU regression"; app.apiVersion = VK_API_VERSION_1_3;
        VkInstanceCreateInfo instance{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO}; instance.pApplicationInfo = &app;
        uint32_t count{}; VK_CHECK(vkEnumerateInstanceLayerProperties(&count, nullptr));
        std::vector<VkLayerProperties> layers(count); VK_CHECK(vkEnumerateInstanceLayerProperties(&count, layers.data()));
        bool validation = std::any_of(layers.begin(), layers.end(), [](auto& l) { return std::strcmp(l.layerName, "VK_LAYER_KHRONOS_validation") == 0; });
        const char* layer = "VK_LAYER_KHRONOS_validation"; const char* extension = VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
        if (validation) { instance.enabledLayerCount = instance.enabledExtensionCount = 1; instance.ppEnabledLayerNames = &layer; instance.ppEnabledExtensionNames = &extension; }
        VK_CHECK(vkCreateInstance(&instance, nullptr, &context.instance)); volkLoadInstance(context.instance);
        if (validation) {
            VkDebugUtilsMessengerCreateInfoEXT debug{VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
            debug.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
            debug.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT;
            debug.pfnUserCallback = Debug;
            VK_CHECK(vkCreateDebugUtilsMessengerEXT(context.instance, &debug, nullptr, &messenger));
        }
        VK_CHECK(vkEnumeratePhysicalDevices(context.instance, &count, nullptr));
        std::vector<VkPhysicalDevice> devices(count); VK_CHECK(vkEnumeratePhysicalDevices(context.instance, &count, devices.data()));
        Require(count > 0, "Vulkan device is available"); context.physicalDevice = devices.front();
        VkPhysicalDeviceProperties props{}; vkGetPhysicalDeviceProperties(context.physicalDevice, &props);
        std::cout << "GPU: " << props.deviceName << "; validation " << (validation ? "enabled" : "unavailable") << '\n';
        vkGetPhysicalDeviceQueueFamilyProperties(context.physicalDevice, &count, nullptr);
        std::vector<VkQueueFamilyProperties> families(count); vkGetPhysicalDeviceQueueFamilyProperties(context.physicalDevice, &count, families.data());
        while (context.graphicsFamilyIndex < count && !(families[context.graphicsFamilyIndex].queueFlags & VK_QUEUE_GRAPHICS_BIT)) ++context.graphicsFamilyIndex;
        Require(context.graphicsFamilyIndex < count, "Graphics queue is available");
        VkPhysicalDeviceScalarBlockLayoutFeatures scalar{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SCALAR_BLOCK_LAYOUT_FEATURES}; scalar.scalarBlockLayout = VK_TRUE;
        VkPhysicalDeviceVulkan13Features features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
        features.dynamicRendering = VK_TRUE; features.pNext = &scalar;
        float priority = 1;
        VkDeviceQueueCreateInfo queue{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
        queue.queueFamilyIndex = context.graphicsFamilyIndex; queue.queueCount = 1; queue.pQueuePriorities = &priority;
        VkDeviceCreateInfo device{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO}; device.pNext = &features;
        VkPhysicalDeviceFeatures supported{}; vkGetPhysicalDeviceFeatures(context.physicalDevice, &supported);
        Require(supported.independentBlend, "Per-attachment skybox write masks are supported");
        VkPhysicalDeviceFeatures enabled{}; enabled.independentBlend = VK_TRUE; device.pEnabledFeatures = &enabled;
        device.queueCreateInfoCount = 1; device.pQueueCreateInfos = &queue;
        VK_CHECK(vkCreateDevice(context.physicalDevice, &device, nullptr, &context.device)); volkLoadDevice(context.device);
        vkGetDeviceQueue(context.device, context.graphicsFamilyIndex, 0, &context.graphicsQueue);
        allocator = labut2::create_allocator(context);
        VkCommandPoolCreateInfo p{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO}; p.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT; p.queueFamilyIndex = context.graphicsFamilyIndex;
        VK_CHECK(vkCreateCommandPool(context.device, &p, nullptr, &pool));
        VkCommandBufferAllocateInfo c{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO}; c.commandPool = pool; c.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; c.commandBufferCount = 1;
        VK_CHECK(vkAllocateCommandBuffers(context.device, &c, &command));
    }
    ~Gpu() {
        vkDeviceWaitIdle(context.device);
        images.clear(); buffers.clear(); allocator = {};
        vkDestroyCommandPool(context.device, pool, nullptr); vkDestroyDevice(context.device, nullptr);
        if (messenger) vkDestroyDebugUtilsMessengerEXT(context.instance, messenger, nullptr);
        vkDestroyInstance(context.instance, nullptr);
    }
    void Begin() {
        VK_CHECK(vkResetCommandBuffer(command, 0));
        VkCommandBufferBeginInfo b{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO}; b.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        VK_CHECK(vkBeginCommandBuffer(command, &b));
    }
    void Submit() {
        VK_CHECK(vkEndCommandBuffer(command));
        VkSubmitInfo s{VK_STRUCTURE_TYPE_SUBMIT_INFO}; s.commandBufferCount = 1; s.pCommandBuffers = &command;
        VK_CHECK(vkQueueSubmit(context.graphicsQueue, 1, &s, VK_NULL_HANDLE)); VK_CHECK(vkQueueWaitIdle(context.graphicsQueue));
    }
    void Barrier(VkImage image, VkImageLayout from, VkImageLayout to, uint32_t levels = 1, uint32_t layers = 1,
        VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT) {
        VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER}; b.oldLayout = from; b.newLayout = to;
        b.srcAccessMask = from == VK_IMAGE_LAYOUT_UNDEFINED ? 0 : VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
        b.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
        b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.image = image; b.subresourceRange = {aspect, 0, levels, 0, layers};
        vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr, 1, &b);
    }
    labut2::Buffer& Buffer(size_t size, VkBufferUsageFlags usage, const void* data = nullptr) {
        buffers.push_back(labut2::create_buffer(allocator, size, usage, VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT, VMA_MEMORY_USAGE_AUTO));
        auto& b = buffers.back();
        if (data) { void* mapped{}; VK_CHECK(vmaMapMemory(allocator.allocator, b.allocation, &mapped)); std::memcpy(mapped, data, size); vmaFlushAllocation(allocator.allocator, b.allocation, 0, size); vmaUnmapMemory(allocator.allocator, b.allocation); }
        return b;
    }
    labut2::ImageWithView& Image(uint32_t size, VkFormat format, uint32_t layers = 1, bool cube = false) {
        VkImageCreateInfo i{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO}; i.flags = cube ? VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT : 0;
        i.imageType = VK_IMAGE_TYPE_2D; i.format = format; i.extent = {size, size, 1}; i.mipLevels = 1; i.arrayLayers = layers;
        i.samples = VK_SAMPLE_COUNT_1_BIT; i.tiling = VK_IMAGE_TILING_OPTIMAL;
        i.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        if (format != VK_FORMAT_D32_SFLOAT) i.usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        else i.usage |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
        VmaAllocationCreateInfo a{}; a.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
        VkImage image{}; VmaAllocation allocation{}; VK_CHECK(vmaCreateImage(allocator.allocator, &i, &a, &image, &allocation, nullptr));
        VkImageViewCreateInfo v{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO}; v.image = image; v.format = format;
        v.viewType = cube ? VK_IMAGE_VIEW_TYPE_CUBE : layers > 1 ? VK_IMAGE_VIEW_TYPE_2D_ARRAY : VK_IMAGE_VIEW_TYPE_2D;
        v.subresourceRange = {VkImageAspectFlags(format == VK_FORMAT_D32_SFLOAT ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT), 0, 1, 0, layers};
        VkImageView view{}; VK_CHECK(vkCreateImageView(context.device, &v, nullptr, &view));
        images.emplace_back(allocator.allocator, image, allocation, view); return images.back();
    }
    void Upload(VkImage image, uint32_t size, uint32_t layers, const void* pixels, size_t bytes, VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT) {
        VkBuffer staging = Buffer(bytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, pixels).buffer;
        Begin(); Barrier(image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, layers, aspect);
        VkBufferImageCopy copy{}; copy.imageSubresource = {aspect, 0, 0, layers}; copy.imageExtent = {size, size, 1};
        vkCmdCopyBufferToImage(command, staging, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
        Barrier(image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 1, layers, aspect); Submit();
    }
    std::vector<Pixel> Read(VkImage image, uint32_t size, uint32_t mip = 0, uint32_t layers = 1, uint32_t levels = 1,
        VkImageLayout layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, bool unorm = false) {
        auto& b = Buffer(size_t(size) * size * layers * (unorm ? 4 : 8), VK_BUFFER_USAGE_TRANSFER_DST_BIT);
        Begin(); Barrier(image, layout, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, levels, layers);
        VkBufferImageCopy copy{}; copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, mip, 0, layers}; copy.imageExtent = {size, size, 1};
        vkCmdCopyImageToBuffer(command, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, b.buffer, 1, &copy);
        Barrier(image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, layout, levels, layers); Submit();
        void* mapped{}; VK_CHECK(vmaMapMemory(allocator.allocator, b.allocation, &mapped));
        vmaInvalidateAllocation(allocator.allocator, b.allocation, 0, VK_WHOLE_SIZE);
        auto* halves = static_cast<uint16_t*>(mapped); std::vector<Pixel> pixels(size_t(size) * size * layers);
        for (size_t i = 0; i < pixels.size(); ++i) for (size_t c = 0; c < 4; ++c)
            pixels[i][c] = unorm ? static_cast<uint8_t*>(mapped)[i * 4 + c] / 255.f : Half(halves[i * 4 + c]);
        vmaUnmapMemory(allocator.allocator, b.allocation); return pixels;
    }
    labut2::Pipeline Pipeline(VkPipelineLayout layout, bool pbr, bool ldr = false, bool portal = false, bool skybox = false) {
        using Shader = labut2::UniqueHandle<VkShaderModule, VkDevice, vkDestroyShaderModule>;
        std::array<Shader, 2> modules;
        VkPipelineShaderStageCreateInfo stages[2]{};
        const char* names[2] = {skybox ? "skybox.vert.spv" : portal ? "portal.vert.spv" : pbr ? "default.vert.spv" : "fullscreen.vert.spv",
            skybox ? "skybox.frag.spv" : portal ? "portal.frag.spv" : pbr ? "default.frag.spv" : "composite.frag.spv"};
        for (int i = 0; i < 2; ++i) {
            auto code = labut2::load_file_u32((shaders + "/" + names[i]).c_str());
            VkShaderModuleCreateInfo m{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO}; m.codeSize = code.size() * 4; m.pCode = code.data();
            VkShaderModule raw{}; VK_CHECK(vkCreateShaderModule(context.device, &m, nullptr, &raw)); modules[i] = Shader(context.device, raw);
            stages[i] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO}; stages[i].stage = i ? VK_SHADER_STAGE_FRAGMENT_BIT : VK_SHADER_STAGE_VERTEX_BIT;
            stages[i].module = raw; stages[i].pName = "main";
        }
        VkBool32 ldrValue = ldr;
        VkSpecializationMapEntry entry{0, 0, sizeof(ldrValue)};
        VkSpecializationInfo specialization{1, &entry, sizeof(ldrValue), &ldrValue};
        if (pbr && !portal && !skybox) stages[1].pSpecializationInfo = &specialization;
        VkVertexInputBindingDescription binding{0, 8 * sizeof(float), VK_VERTEX_INPUT_RATE_VERTEX};
        VkVertexInputAttributeDescription attributes[] = {{0,0,VK_FORMAT_R32G32B32_SFLOAT,0}, {1,0,VK_FORMAT_R32G32_SFLOAT,12}, {2,0,VK_FORMAT_R32G32B32_SFLOAT,20}};
        VkPipelineVertexInputStateCreateInfo vertex{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
        if (pbr) { vertex.vertexBindingDescriptionCount = 1; vertex.pVertexBindingDescriptions = &binding; vertex.vertexAttributeDescriptionCount = skybox ? 1 : 3; vertex.pVertexAttributeDescriptions = attributes; }
        VkPipelineInputAssemblyStateCreateInfo assembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO}; assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        VkPipelineViewportStateCreateInfo viewport{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO}; viewport.viewportCount = viewport.scissorCount = 1;
        VkPipelineRasterizationStateCreateInfo raster{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO}; raster.polygonMode = VK_POLYGON_MODE_FILL; raster.lineWidth = 1;
        VkPipelineMultisampleStateCreateInfo samples{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO}; samples.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        VkPipelineColorBlendAttachmentState attachments[3]{}; for (auto& a : attachments) a.colorWriteMask = 15;
        const SkyboxAttachments skyboxAttachments(VK_FORMAT_R16G16B16A16_SFLOAT);
        if (skybox) std::copy(skyboxAttachments.blends.begin(),skyboxAttachments.blends.end(),attachments);
        VkPipelineColorBlendStateCreateInfo blend{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO}; blend.attachmentCount = pbr && !ldr ? 3 : 1; blend.pAttachments = attachments;
        VkDynamicState states[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        VkPipelineDynamicStateCreateInfo dynamic{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO}; dynamic.dynamicStateCount = 2; dynamic.pDynamicStates = states;
        VkFormat formats[3] = {ldr ? VK_FORMAT_R8G8B8A8_UNORM : VK_FORMAT_R16G16B16A16_SFLOAT, VK_FORMAT_R16G16B16A16_SFLOAT, VK_FORMAT_R16G16B16A16_SFLOAT};
        if (skybox) std::copy(skyboxAttachments.formats.begin(),skyboxAttachments.formats.end(),formats);
        VkPipelineRenderingCreateInfo rendering{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO}; rendering.colorAttachmentCount = blend.attachmentCount; rendering.pColorAttachmentFormats = formats;
        VkPipelineDepthStencilStateCreateInfo depth{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
        if (skybox) { rendering.depthAttachmentFormat = VK_FORMAT_D32_SFLOAT; depth.depthTestEnable = VK_TRUE; depth.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL; }
        VkGraphicsPipelineCreateInfo info{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO}; info.pNext = &rendering; info.layout = layout; info.stageCount = 2; info.pStages = stages;
        info.pVertexInputState = &vertex; info.pInputAssemblyState = &assembly; info.pViewportState = &viewport; info.pRasterizationState = &raster;
        info.pMultisampleState = &samples; info.pColorBlendState = &blend; info.pDynamicState = &dynamic;
        if (skybox) info.pDepthStencilState = &depth;
        VkPipeline pipeline{}; VK_CHECK(vkCreateGraphicsPipelines(context.device, {}, 1, &info, nullptr, &pipeline)); return labut2::Pipeline(context.device, pipeline);
    }
};
void Near(float value, float expected, float tolerance, const char* message) {
    if (!std::isfinite(value) || std::abs(value - expected) > tolerance) {
        std::cerr << "Actual " << value << ", expected " << expected << '\n'; throw std::runtime_error(message);
    }
}
void TestShading(Gpu& gpu, const IblResources& ibl, bool whiteEnvironment = true, bool patternEnvironment = false) {
    const VkDevice device = gpu.context.device;
    auto makeLayout = [&](std::vector<VkDescriptorSetLayoutBinding> bindings) {
        VkDescriptorSetLayoutCreateInfo info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO}; info.bindingCount = uint32_t(bindings.size()); info.pBindings = bindings.data();
        VkDescriptorSetLayout raw{}; VK_CHECK(vkCreateDescriptorSetLayout(device, &info, nullptr, &raw)); return labut2::DescriptorSetLayout(device, raw);
    };
    std::vector<VkDescriptorSetLayoutBinding> sceneBindings, materialBindings, compositeBindings;
    for (uint32_t i = 0; i < 5; ++i) {
        sceneBindings.push_back({i, i ? VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER : VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1,
            VkShaderStageFlags(i ? VK_SHADER_STAGE_FRAGMENT_BIT : VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT), nullptr});
        compositeBindings.push_back({i, i == 2 ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER : VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr});
    }
    for (uint32_t i = 0; i < 6; ++i) materialBindings.push_back({i, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr});
    auto sceneLayout = makeLayout(sceneBindings), materialLayout = makeLayout(materialBindings), compositeLayout = makeLayout(compositeBindings);
    const auto skyboxBindings = SkyboxDescriptorBindings();
    Require((skyboxBindings[0].stageFlags & VK_SHADER_STAGE_FRAGMENT_BIT) != 0,
        "Skybox environment UBO is visible to the fragment shader");
    auto skyboxSetLayout = makeLayout({skyboxBindings.begin(),skyboxBindings.end()});
    auto makePipelineLayout = [&](std::vector<VkDescriptorSetLayout> sets, uint32_t size, VkShaderStageFlags stages) {
        VkPushConstantRange push{stages,0,size}; VkPipelineLayoutCreateInfo info{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        info.setLayoutCount = uint32_t(sets.size()); info.pSetLayouts = sets.data(); info.pushConstantRangeCount = size ? 1 : 0; info.pPushConstantRanges = size ? &push : nullptr;
        VkPipelineLayout raw{}; VK_CHECK(vkCreatePipelineLayout(device, &info, nullptr, &raw)); return labut2::PipelineLayout(device, raw);
    };
    auto pbrLayout = makePipelineLayout({sceneLayout.handle, materialLayout.handle}, 128, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT);
    auto compLayout = makePipelineLayout({compositeLayout.handle}, sizeof(glsl::CompositeParameters), VK_SHADER_STAGE_FRAGMENT_BIT);
    auto skyboxLayout = makePipelineLayout({skyboxSetLayout.handle},0,0);
    VkDescriptorPoolSize sizes[] = {{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,3},{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,16}};
    VkDescriptorPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO}; poolInfo.maxSets = 4; poolInfo.poolSizeCount = 2; poolInfo.pPoolSizes = sizes;
    VkDescriptorPool rawPool{}; VK_CHECK(vkCreateDescriptorPool(device, &poolInfo, nullptr, &rawPool)); labut2::DescriptorPool pool(device,rawPool);
    auto allocate = [&](VkDescriptorSetLayout layout) { VkDescriptorSetAllocateInfo info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO}; info.descriptorPool = rawPool; info.descriptorSetCount = 1; info.pSetLayouts = &layout; VkDescriptorSet set{}; VK_CHECK(vkAllocateDescriptorSets(device, &info, &set)); return set; };
    VkDescriptorSet sceneSet = allocate(sceneLayout.handle), materialSet = allocate(materialLayout.handle), compositeSet = allocate(compositeLayout.handle);
    VkDescriptorSet skyboxSet = allocate(skyboxSetLayout.handle);
    VkSamplerCreateInfo samplerInfo{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO}; samplerInfo.magFilter = samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = samplerInfo.addressModeV = samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    VkSampler rawSampler{}; VK_CHECK(vkCreateSampler(device, &samplerInfo, nullptr, &rawSampler)); labut2::Sampler sampler(device, rawSampler);
    samplerInfo.compareEnable = VK_TRUE; samplerInfo.compareOp = VK_COMPARE_OP_LESS;
    VkSampler rawShadow{}; VK_CHECK(vkCreateSampler(device, &samplerInfo, nullptr, &rawShadow)); labut2::Sampler shadowSampler(device, rawShadow);
    auto bindImage = [&](VkDescriptorSet set, uint32_t binding, VkImageView view, VkSampler s) {
        VkDescriptorImageInfo image{s,view,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}; VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        write.dstSet = set; write.dstBinding = binding; write.descriptorCount = 1; write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; write.pImageInfo = &image;
        vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
    };
    auto bindBuffer = [&](VkDescriptorSet set, uint32_t binding, VkBuffer buffer, size_t size) {
        VkDescriptorBufferInfo b{buffer,0,size}; VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET}; write.dstSet = set; write.dstBinding = binding;
        write.descriptorCount = 1; write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER; write.pBufferInfo = &b; vkUpdateDescriptorSets(device,1,&write,0,nullptr);
    };
    auto texture = [&](Pixel pixel) { auto& image = gpu.Image(1, VK_FORMAT_R32G32B32A32_SFLOAT); const VkImageView view = image.view; gpu.Upload(image.image,1,1,&pixel,sizeof(pixel)); return view; };
    const auto white = texture({1,1,1,1}), black = texture({0,0,0,0}), normal = texture({.5f,.5f,1,1}), hdr = texture({4,4,4,1});
    auto& shadow = gpu.Image(1,VK_FORMAT_D32_SFLOAT,4); const VkImageView shadowView = shadow.view; const float depth[4] = {1,1,1,1};
    gpu.Upload(shadow.image,1,4,depth,sizeof(depth),VK_IMAGE_ASPECT_DEPTH_BIT);
    struct Scene {
        glm::mat4 camera{1}, projection{1}, projCam{1}; glm::vec4 cameraPos{0,0,10000,1}, lights[64]{}, lightPos{}, lightColor{};
        uint32_t lightCount{}, renderMode{}, iblEnabled{}, pad{}; glm::mat4 lightVP[4]{}; glm::vec4 cascadeSplits{}, clip{};
        glsl::EnvironmentParameters environment;
    } scene;
    struct Object {
        glm::mat4 transform{1}; glm::vec4 base{1}, emissive{0,0,0,1}; float metallic = 1, roughness = 1, cutoff = 0, pad = 0; glm::vec4 clip{};
    } object;
    static_assert(sizeof(Scene) == 1600 && offsetof(Scene,environment) == 1568 && sizeof(Object) == 128);
    const VkBuffer ubo = gpu.Buffer(sizeof(scene),VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,&scene).buffer;
    // All draws finish before their uniform storage is reused.
    auto updateScene = [&]() { auto it = std::find_if(gpu.buffers.begin(),gpu.buffers.end(),[&](auto& b) { return b.buffer == ubo; }); void* p{}; VK_CHECK(vmaMapMemory(gpu.allocator.allocator,it->allocation,&p)); std::memcpy(p,&scene,sizeof(scene)); vmaFlushAllocation(gpu.allocator.allocator,it->allocation,0,sizeof(scene)); vmaUnmapMemory(gpu.allocator.allocator,it->allocation); };
    bindBuffer(sceneSet,0,ubo,sizeof(scene)); bindImage(sceneSet,1,shadowView,rawShadow);
    bindBuffer(skyboxSet,0,ubo,sizeof(scene));
    bindImage(sceneSet,2,ibl.prefilteredEnvMap.view,ibl.sampler.handle); bindImage(sceneSet,3,ibl.irradianceMap.view,ibl.sampler.handle); bindImage(sceneSet,4,ibl.brdfLut.view,ibl.sampler.handle);
    for (uint32_t i = 0; i < 6; ++i) bindImage(materialSet,i,i == 4 ? normal : white,rawSampler);
    struct Vertex { float x,y,z,u,v,nx,ny,nz; } vertices[] = {{-1,-1,.5f,0,0,0,0,1},{-1,3,.5f,0,2,0,0,1},{3,-1,.5f,2,0,0,0,1}};
    const VkBuffer vertexBuffer = gpu.Buffer(sizeof(vertices),VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,vertices).buffer;
    auto hdrPipeline = gpu.Pipeline(pbrLayout.handle,true), ldrPipeline = gpu.Pipeline(pbrLayout.handle,true,true), compositePipeline = gpu.Pipeline(compLayout.handle,false);
    auto draw = [&](VkPipeline pipeline, VkPipelineLayout layout, const void* push, uint32_t pushBytes, bool pbr, bool ldr = false, bool skybox = false, float depthClear = 1.f) {
        constexpr uint32_t extent = 8; const uint32_t count = pbr && !ldr ? 3 : 1;
        std::array<VkImage,3> targets{}; std::array<VkRenderingAttachmentInfo,3> attachments{};
        for (uint32_t i = 0; i < count; ++i) {
            auto& image = gpu.Image(extent, ldr ? VK_FORMAT_R8G8B8A8_UNORM : VK_FORMAT_R16G16B16A16_SFLOAT); targets[i] = image.image;
            attachments[i] = {VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO}; attachments[i].imageView = image.view; attachments[i].imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            attachments[i].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR; attachments[i].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            if (skybox) attachments[i].clearValue.color = { .0625f, .125f, .25f, 1.f };
        }
        VkImage depthImage{}; VkRenderingAttachmentInfo depthAttachment{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
        if (skybox) {
            auto& image = gpu.Image(extent,VK_FORMAT_D32_SFLOAT); depthImage = image.image;
            depthAttachment.imageView = image.view; depthAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
            depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR; depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            depthAttachment.clearValue.depthStencil = {depthClear,0};
        }
        gpu.Begin(); for (uint32_t i = 0; i < count; ++i) gpu.Barrier(targets[i],VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        if (skybox) gpu.Barrier(depthImage,VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,1,1,VK_IMAGE_ASPECT_DEPTH_BIT);
        VkRenderingInfo info{VK_STRUCTURE_TYPE_RENDERING_INFO}; info.renderArea.extent = {extent,extent}; info.layerCount = 1; info.colorAttachmentCount = count; info.pColorAttachments = attachments.data();
        if (skybox) info.pDepthAttachment = &depthAttachment;
        vkCmdBeginRendering(gpu.command,&info); vkCmdBindPipeline(gpu.command,VK_PIPELINE_BIND_POINT_GRAPHICS,pipeline);
        VkViewport viewport{0,0,float(extent),float(extent),0,1}; VkRect2D scissor{{0,0},{extent,extent}};
        vkCmdSetViewport(gpu.command,0,1,&viewport); vkCmdSetScissor(gpu.command,0,1,&scissor);
        VkDescriptorSet sets[] = {skybox ? skyboxSet : pbr ? sceneSet : compositeSet, materialSet};
        vkCmdBindDescriptorSets(gpu.command,VK_PIPELINE_BIND_POINT_GRAPHICS,layout,0,skybox ? 1 : pbr ? 2 : 1,sets,0,nullptr);
        if (pbr) { VkDeviceSize offset = 0; vkCmdBindVertexBuffers(gpu.command,0,1,&vertexBuffer,&offset); }
        if (!skybox) vkCmdPushConstants(gpu.command,layout,pbr ? VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT : VK_SHADER_STAGE_FRAGMENT_BIT,0,pushBytes,push);
        vkCmdDraw(gpu.command,3,1,0,0); vkCmdEndRendering(gpu.command); gpu.Submit();
        auto frame = gpu.Read(targets[0],extent,0,1,1,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,ldr);
        for (auto& pixel : frame) for (float c : pixel) Require(std::isfinite(c),"Every production shader output pixel remains finite");
        if (count == 3) for (uint32_t i = 1; i < count; ++i)
            for (auto& pixel : gpu.Read(targets[i],extent,0,1,1,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL))
                for (uint32_t c = 0; c < 4; ++c) {
                    Require(std::isfinite(pixel[c]),"Bloom/normal attachments remain finite including sharp GGX peaks");
                    if (skybox) Near(pixel[c],attachments[i].clearValue.color.float32[c],.0001f,"Skybox preserves Bloom and normal buffers");
                }
        return frame[extent / 2 * extent + extent / 2];
    };
    if (whiteEnvironment) {
        const Pixel cubePixels[6] = {{4,2,.5f,1},{.25f,3,1,1},{4,2,.5f,1},{4,2,.5f,1},{4,2,.5f,1},{4,2,.5f,1}};
        auto& cube = gpu.Image(1,VK_FORMAT_R32G32B32A32_SFLOAT,6,true);
        const VkImageView cubeView = cube.view; gpu.Upload(cube.image,1,6,cubePixels,sizeof(cubePixels));
        bindImage(skyboxSet,1,cubeView,rawSampler);
        auto skyboxPipeline = gpu.Pipeline(skyboxLayout.handle,true,false,false,true);
        const auto sky = draw(skyboxPipeline.handle,skyboxLayout.handle,nullptr,0,true,false,true);
        Near(sky[0],4,.005f,"Skybox color remains linear HDR in the scene MRT");
        Near(sky[1],2,.005f,"Skybox retains source channel ratios");
        Near(sky[2],.5f,.002f,"Skybox uses the float attachment without sRGB encoding");
        const auto occluded = draw(skyboxPipeline.handle,skyboxLayout.handle,nullptr,0,true,false,true,.5f);
        Near(occluded[0],.0625f,.0001f,"Foreground depth prevents skybox overwrite");
        Near(occluded[1],.125f,.0001f,"Foreground depth preserves scene color");
        engine::EnvironmentLightingSettings settings;
        settings.rotationDegrees = 90;
        scene.environment = engine::MakeEnvironmentParameters(settings); updateScene();
        const auto rotated = draw(skyboxPipeline.handle,skyboxLayout.handle,nullptr,0,true,false,true);
        Near(rotated[0],.25f,.005f,"Skybox rotation selects the inverse-rotated cube face");
        Near(rotated[1],3,.005f,"Skybox rotation is updated without rebaking");
        settings.rotationDegrees = 0; settings.intensity = 2; settings.saturation = 0; settings.tint = {.5f,1,1};
        scene.environment = engine::MakeEnvironmentParameters(settings); updateScene();
        const auto tinted = draw(skyboxPipeline.handle,skyboxLayout.handle,nullptr,0,true,false,true);
        Near(tinted[0],2.3169f,.01f,"Skybox applies saturation and RGB tint in linear space");
        Near(tinted[1],4.6338f,.01f,"Skybox applies global environment intensity");
        scene.environment = {}; updateScene();
        std::cout << "[PASS] Skybox: dedicated production descriptors, fragment UBO controls, three HDR attachments and foreground occlusion.\n";
    }
    if (patternEnvironment) {
        object.metallic = 1; object.roughness = .02f; scene.iblEnabled = 1; updateScene();
        const auto initial = draw(hdrPipeline.handle,pbrLayout.handle,&object,sizeof(object),true);
        Require(initial[0] > .9f && initial[1] < .05f && initial[2] > .9f,"IBL initially reflects the +Z magenta face");
        engine::EnvironmentLightingSettings settings; settings.rotationDegrees = 90;
        scene.environment = engine::MakeEnvironmentParameters(settings); updateScene();
        const auto rotated = draw(hdrPipeline.handle,pbrLayout.handle,&object,sizeof(object),true);
        Require(rotated[0] < .05f && rotated[1] > .9f && rotated[2] < .05f,"IBL rotation selects the same -X green face as the skybox");
        std::cout << "[PASS] Live IBL rotation matches the skybox coordinate convention.\n";
        return;
    }
    // An emissive-only surface must keep radiance above one through the scene pass.
    object.emissive = {.8f,.8f,.8f,1};
    Near(draw(hdrPipeline.handle,pbrLayout.handle,&object,sizeof(object),true)[0],4,.005f,"PBR scene preserves linear HDR radiance");
    Near(draw(ldrPipeline.handle,pbrLayout.handle,&object,sizeof(object),true,true)[0],.988f,.005f,"Thumbnail specialization converts HDR once to sRGB UNORM");
    object.emissive = {0,0,0,1}; scene.iblEnabled = 1; updateScene();
    if (!whiteEnvironment) {
        for (float metallic : {0.f, 1.f}) {
            object.metallic = metallic; object.roughness = .5f;
            scene.iblEnabled = 1; updateScene();
            const auto enabled = draw(hdrPipeline.handle,pbrLayout.handle,&object,sizeof(object),true);
            for (uint32_t c = 0; c < 3; ++c)
                Require(enabled[c] > .005f && enabled[c] < 1.f,"Project skybox illuminates both nonmetal and metal without direct lights");
            scene.iblEnabled = 0; updateScene();
            const auto disabled = draw(hdrPipeline.handle,pbrLayout.handle,&object,sizeof(object),true);
            for (uint32_t c = 0; c < 3; ++c) Near(disabled[c],0,.0001f,"IBL toggle removes actual project environment illumination");
            std::cout << "[PASS] Actual skybox IBL toggle, metallic=" << metallic << ": RGB "
                << enabled[0] << ", " << enabled[1] << ", " << enabled[2] << " -> 0.\n";
        }
        object.metallic = 1; scene.iblEnabled = 1;
        engine::EnvironmentLightingSettings settings; settings.saturation = 0;
        scene.environment = engine::MakeEnvironmentParameters(settings); updateScene();
        const auto grayscale = draw(hdrPipeline.handle,pbrLayout.handle,&object,sizeof(object),true);
        Require(grayscale[0] > .01f,"Desaturated project IBL retains illumination");
        Near(grayscale[0],grayscale[1],.002f,"Environment saturation removes project sky color cast");
        Near(grayscale[1],grayscale[2],.002f,"Environment saturation keeps neutral metal neutral");
        std::cout << "[PASS] Live environment saturation on the actual project skybox.\n";
        return;
    }
    engine::EnvironmentLightingSettings environment;
    environment.intensity = 0;
    scene.environment = engine::MakeEnvironmentParameters(environment); updateScene();
    Near(draw(hdrPipeline.handle,pbrLayout.handle,&object,sizeof(object),true)[0],0,.0001f,"Zero environment intensity removes IBL");
    environment.intensity = 2; environment.specularIntensity = .5f;
    scene.environment = engine::MakeEnvironmentParameters(environment); updateScene();
    Near(draw(hdrPipeline.handle,pbrLayout.handle,&object,sizeof(object),true)[0],1,.015f,"Global and specular IBL strengths multiply consistently");
    environment.specularIntensity = 0;
    scene.environment = engine::MakeEnvironmentParameters(environment); updateScene();
    Near(draw(hdrPipeline.handle,pbrLayout.handle,&object,sizeof(object),true)[0],0,.0001f,"Specular IBL can be independently disabled on metal");
    object.metallic = 0; environment.diffuseIntensity = .5f;
    scene.environment = engine::MakeEnvironmentParameters(environment); updateScene();
    Near(draw(hdrPipeline.handle,pbrLayout.handle,&object,sizeof(object),true)[0],.96f,.015f,"Diffuse IBL has an independent strength control");
    environment = {}; environment.tint = {.5f,1,.25f}; object.metallic = 1;
    scene.environment = engine::MakeEnvironmentParameters(environment); updateScene();
    const auto tint = draw(hdrPipeline.handle,pbrLayout.handle,&object,sizeof(object),true);
    Near(tint[0],.5f,.015f,"IBL applies the environment red multiplier");
    Near(tint[1],1,.015f,"IBL preserves the unchanged green multiplier");
    Near(tint[2],.25f,.015f,"IBL applies the environment blue multiplier");
    scene.environment = {}; updateScene();
    std::cout << "[PASS] Live IBL controls: global/diffuse/specular intensity and tint.\n";
    for (float roughness : {.02f,.25f,.5f,.75f,1.f}) {
        object.roughness = roughness;
        const auto p = draw(hdrPipeline.handle,pbrLayout.handle,&object,sizeof(object),true);
        for (uint32_t c = 0; c < 3; ++c) Near(p[c],1,.015f,"Compensated white metal passes GPU white-furnace test across roughness");
    }
    scene.cameraPos = {10000,0,1000,1}; updateScene();
    Near(draw(hdrPipeline.handle,pbrLayout.handle,&object,sizeof(object),true)[0],1,.015f,"White furnace also holds at a grazing view");
    scene.cameraPos = {0,0,10000,1};
    // Direct lighting must use the GGX lobe, even with environment lighting off.
    scene.iblEnabled = 0; scene.lightCount = 1; scene.lights[0] = {0,0,1,0}; scene.lights[1] = {1,1,1,1}; scene.lights[3].z = 1; updateScene();
    object.roughness = 1;
    auto direct = draw(hdrPipeline.handle,pbrLayout.handle,&object,sizeof(object),true);
    Require(std::isfinite(direct[0]) && direct[0] > .1f && direct[0] < .4f,"Direct GGX rough highlight is finite and nonzero with IBL disabled");
    object.roughness = .02f;
    auto peak = draw(hdrPipeline.handle,pbrLayout.handle,&object,sizeof(object),true);
    Require(peak[0] > 60000 && peak[0] <= 65504,"Sharp white-metal highlight stays within RGBA16F range");
    scene.lights[0] = {.9848078f,0,.1736482f,0}; object.roughness = .2f; updateScene();
    auto tail = draw(hdrPipeline.handle,pbrLayout.handle,&object,sizeof(object),true);
    Require(tail[0] > .0004f && tail[0] < .0012f,"Direct GGX retains its long tail away from the mirror direction");
    scene.lights[3].z = 0; updateScene();
    Near(draw(hdrPipeline.handle,pbrLayout.handle,&object,sizeof(object),true)[0],0,.0001f,"Per-light specular multiplier remains effective");
    std::cout << "[PASS] Production PBR HDR, PNG specialization, white furnace and direct GGX/specular controls.\n";
    // The portal shaders share the scene UBO / material layout, but copy HDR
    // radiance instead of shading it. Verify their surface survives near clipping.
    auto portalPipeline = gpu.Pipeline(pbrLayout.handle,true,false,true);
    bindImage(materialSet,0,hdr,rawSampler);
    object = {}; object.base = glm::vec4(0);
    auto portal = draw(portalPipeline.handle,pbrLayout.handle,&object,80,true);
    Near(portal[0],4.12f,.015f,"Portal surface copies HDR radiance without display conversion");
    Near(portal[3],1,.001f,"Portal surface remains opaque inside the mask");
    object.transform[3].z = -.51f;
    portal = draw(portalPipeline.handle,pbrLayout.handle,&object,80,true);
    Near(portal[0],4.12f,.015f,"Portal surface survives crossing the camera near plane");
    Near(portal[3],1,.001f,"Near-plane portal keeps its surface coverage");
    std::cout << "[PASS] Production portal shaders: HDR copy, opaque mask and near-plane coverage.\n";
    struct Mosaic { int enabled = 0; float size = 1; } mosaic;
    bindBuffer(compositeSet,2,gpu.Buffer(sizeof(mosaic),VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,&mosaic).buffer,sizeof(mosaic));
    bindImage(compositeSet,0,hdr,rawSampler); bindImage(compositeSet,1,black,rawSampler); bindImage(compositeSet,3,black,rawSampler); bindImage(compositeSet,4,white,rawSampler);
    auto comp = engine::MakeCompositeParameters(.5f,0,false,true,engine::ToneMappingSettings{});
    Near(draw(compositePipeline.handle,compLayout.handle,&comp,sizeof(comp),false)[0],.91485f,.002f,"Composite applies exposure before exactly one ACES curve");
    engine::ToneMappingSettings toneMapping; toneMapping.acesEnabled = false;
    comp = engine::MakeCompositeParameters(.125f,0,false,true,toneMapping);
    Near(draw(compositePipeline.handle,compLayout.handle,&comp,sizeof(comp),false)[0],.5f,.002f,"ACES bypass retains exposure in linear output");
    comp.exposure = .5f;
    Near(draw(compositePipeline.handle,compLayout.handle,&comp,sizeof(comp),false)[0],1,.002f,"ACES bypass safely clips HDR at display white");
    toneMapping.acesEnabled = true; toneMapping.acesCurve = {.8f,.2f,1.4f,.7f,.3f};
    comp = engine::MakeCompositeParameters(.5f,0,false,true,toneMapping);
    Near(draw(compositePipeline.handle,compLayout.handle,&comp,sizeof(comp),false)[0],3.6f/7.3f,.002f,"All five editable ACES coefficients reach the shader");
    toneMapping.acesCurve = {1,0,0,0,0};
    comp = engine::MakeCompositeParameters(.5f,0,false,true,toneMapping);
    Near(draw(compositePipeline.handle,compLayout.handle,&comp,sizeof(comp),false)[0],1,.002f,"ACES denominator guard prevents nonfinite curve output");
    comp = engine::MakeCompositeParameters(.5f,0,false,true,engine::ToneMappingSettings{});
    std::cout << "[PASS] Live ACES controls: bypass, exposure, five curve coefficients and finite denominator.\n";
    comp.exposure = 1;
    const float originalBrightness = draw(compositePipeline.handle,compLayout.handle,&comp,sizeof(comp),false)[0];
    comp.exposure = .35f;
    const float reducedBrightness = draw(compositePipeline.handle,compLayout.handle,&comp,sizeof(comp),false)[0];
    Require(reducedBrightness < originalBrightness,"Lower exposure darkens HDR scene and portal radiance");
    // Midtones reveal excessive exposure more clearly than an HDR highlight
    // already near the tone curve's shoulder. Check both presentation paths.
    bindImage(compositeSet,0,texture({.5f,.5f,.5f,1}),rawSampler);
    for (float srgbOutput : {0.f,1.f}) {
        comp.srgbOutput = srgbOutput; comp.exposure = .7f;
        const auto previousGray = draw(compositePipeline.handle,compLayout.handle,&comp,sizeof(comp),false);
        comp.exposure = .35f;
        const auto currentGray = draw(compositePipeline.handle,compLayout.handle,&comp,sizeof(comp),false);
        Require(currentGray[0] < previousGray[0] * .85f,"Lower exposure visibly darkens midtones in both presentation paths");
        for (uint32_t c = 1; c < 3; ++c) Near(currentGray[c],currentGray[0],.001f,"Exposure keeps neutral materials neutral");
        std::cout << "[PASS] Midtone exposure, sRGB target=" << srgbOutput << ": "
            << previousGray[0] << " -> " << currentGray[0] << ".\n";
    }
    bindImage(compositeSet,0,hdr,rawSampler);
    comp.exposure = .5f;
    comp.srgbOutput = 0;
    Near(draw(compositePipeline.handle,compLayout.handle,&comp,sizeof(comp),false)[0],.9616f,.002f,"UNORM presentation receives sRGB encoding");
    bindImage(compositeSet,0,black,rawSampler); comp.editorBackdrop = 1;
    auto backdrop = draw(compositePipeline.handle,compLayout.handle,&comp,sizeof(comp),false);
    Require(backdrop[0] > .89f && backdrop[0] < .95f,"Editor backdrop remains in its authored display space");
    std::cout << "[PASS] Production composite: HDR exposure, single tone map, display encoding and editor backdrop.\n";
}
void TestBake(Gpu& gpu) {
    constexpr uint32_t size = 32;
    std::vector<Pixel> pixels(size * size * 6, {1, 1, 1, 1});
    auto& source = gpu.Image(size, VK_FORMAT_R32G32B32A32_SFLOAT, 6, true);
    const VkImage image = source.image; const VkImageView view = source.view;
    gpu.Upload(image, size, 6, pixels.data(), pixels.size() * sizeof(Pixel));
    IblBakeSettings settings{32, 8, 32, 512, 1024};
    auto ibl = create_ibl_resources(gpu.context, gpu.allocator, gpu.pool, view, gpu.shaders.c_str(), settings);
    Require(ibl.prefilteredMipLevels == 6, "Complete prefilter mip chain");
    for (uint32_t mip = 0; mip < 6; ++mip)
        for (auto& pixel : gpu.Read(ibl.prefilteredEnvMap.image, size >> mip, mip, 6, 6))
            for (uint32_t c = 0; c < 3; ++c) Near(pixel[c], 1, .005f, "Constant environment stays constant at every roughness");
    for (auto& pixel : gpu.Read(ibl.irradianceMap.image, 8, 0, 6))
        for (uint32_t c = 0; c < 3; ++c) Near(pixel[c], 1, .005f, "Diffuse irradiance/pi normalization");
    const auto lut = gpu.Read(ibl.brdfLut.image, 32);
    for (auto& p : lut) Require(std::isfinite(p[0]) && std::isfinite(p[1]) && p[0] >= 0 && p[1] >= 0 && p[0] + p[1] < 1.04f,
        "DFG integral is finite and bounded including grazing/rough endpoints");
    Require(lut.back()[0] < .4f && lut[31][0] > .99f, "DFG captures rough GGX masking loss");
    std::cout << "[PASS] Constant radiance, all mip levels/faces, diffuse normalization, DFG endpoint/finite checks.\n";
    TestShading(gpu, ibl);

    // Asymmetric face colors catch face swaps; the final mip must mix them.
    const Pixel colors[6] = {{1,0,0,1}, {0,1,0,1}, {0,0,1,1}, {1,1,0,1}, {1,0,1,1}, {0,1,1,1}};
    for (uint32_t face = 0; face < 6; ++face) std::fill_n(pixels.begin() + face * size * size, size * size, colors[face]);
    auto& pattern = gpu.Image(size, VK_FORMAT_R32G32B32A32_SFLOAT, 6, true);
    const VkImageView patternView = pattern.view;
    gpu.Upload(pattern.image, size, 6, pixels.data(), pixels.size() * sizeof(Pixel));
    auto blurred = create_ibl_resources(gpu.context, gpu.allocator, gpu.pool, patternView, gpu.shaders.c_str(), settings);
    TestShading(gpu,blurred,false,true);
    const auto smooth = gpu.Read(blurred.prefilteredEnvMap.image, size, 0, 6, 6);
    const auto rough = gpu.Read(blurred.prefilteredEnvMap.image, 1, 5, 6, 6);
    for (uint32_t face = 0; face < 6; ++face) {
        const auto& center = smooth[face * size * size + (size / 2) * size + size / 2];
        for (uint32_t c = 0; c < 3; ++c) Near(center[c], colors[face][c], .01f, "Cube face orientation");
        float distance = 0; for (uint32_t c = 0; c < 3; ++c) distance += std::abs(rough[face][c] - colors[face][c]);
        Require(distance > .3f, "Rough reflections broaden across cube faces instead of darkening");
    }
    std::cout << "[PASS] Cubemap face identity and rough GGX reflection broadening.\n";
}
void TestProjectSkybox(Gpu& gpu, const char* path) {
    int width{}, height{}, channels{};
    stbi_set_flip_vertically_on_load(0);
    auto* loaded = stbi_load(path,&width,&height,&channels,4);
    Require(loaded != nullptr,"Project skybox PNG loads");
    const uint32_t size = uint32_t(width / 4);
    Require(width % 4 == 0 && height % 3 == 0 && width / 4 == height / 3,"Project skybox has six square cross faces");
    const uint32_t coords[6][2] = {{2,1},{0,1},{1,0},{1,2},{1,1},{3,1}};
    std::vector<uint8_t> pixels(size_t(size) * size * 6 * 4);
    for (uint32_t face = 0; face < 6; ++face) for (uint32_t y = 0; y < size; ++y)
        std::memcpy(pixels.data() + (size_t(face) * size * size + y * size) * 4,
            loaded + (size_t(coords[face][1] * size + y) * width + coords[face][0] * size) * 4, size * 4);
    stbi_image_free(loaded);
    auto& source = gpu.Image(size,VK_FORMAT_R8G8B8A8_SRGB,6,true);
    const VkImageView view = source.view;
    gpu.Upload(source.image,size,6,pixels.data(),pixels.size());
    // Use the exact production format, face extraction and bake resolutions.
    auto ibl = create_ibl_resources(gpu.context,gpu.allocator,gpu.pool,view,gpu.shaders.c_str());
    float minimum = 1.f, maximum = 0.f;
    for (auto& p : gpu.Read(ibl.irradianceMap.image,32,0,6)) {
        const float luminance = .2126f * p[0] + .7152f * p[1] + .0722f * p[2];
        Require(std::isfinite(luminance),"Actual project diffuse convolution is finite");
        minimum = std::min(minimum,luminance); maximum = std::max(maximum,luminance);
    }
    Require(minimum > .005f && maximum < 1.f,"Actual project diffuse cubemap is nonblack in every direction");
    std::cout << "[PASS] Project sRGB PNG convolution luminance: " << minimum << ".." << maximum << ".\n";
    TestShading(gpu,ibl,false);
}
}
int main(int argc, char** argv) try {
    Require(argc == 3, "Usage: PbrGpuTests shader-directory project-skybox");
    {
        Gpu gpu(argv[1]); TestBake(gpu); TestProjectSkybox(gpu,argv[2]);
    }
    Require(Gpu::validationErrors == 0, "No Vulkan validation errors");
    std::cout << "PBR GPU regression passed.\n"; return 0;
} catch (const std::exception& error) { std::cerr << "[FAIL] " << error.what() << '\n'; return 1; }
