// Headless execution of production MSAA attachment resolve and temporal AA.
#include "Runtime/Renderer/RenderUtilities/AntiAliasing.hpp"
#include "Runtime/Renderer/RenderUtilities/SkyboxAttachments.hpp"
#include "Runtime/Renderer/RenderUtilities/RenderParameters.hpp"
#include "Runtime/Rhi/vkbuffer.hpp"
#include "Runtime/Rhi/load.hpp"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
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
        app.pApplicationName = "Steer anti-aliasing GPU regression"; app.apiVersion = VK_API_VERSION_1_3;
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
};
labut2::Pipeline GeometryPipeline(Gpu& gpu, VkPipelineLayout layout, VkSampleCountFlagBits samples) {
    using Shader = labut2::UniqueHandle<VkShaderModule, VkDevice, vkDestroyShaderModule>;
    std::array<Shader,2> owners;
    VkPipelineShaderStageCreateInfo stages[2]{};
    const char* names[]{"aa_triangle.vert.spv", "aa_triangle.frag.spv"};
    for (uint32_t i=0; i<2; ++i) {
        auto code = labut2::load_file_u32((gpu.shaders + "/" + names[i]).c_str());
        VkShaderModuleCreateInfo m{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO}; m.codeSize=code.size()*4; m.pCode=code.data();
        VkShaderModule module{}; VK_CHECK(vkCreateShaderModule(gpu.context.device,&m,nullptr,&module)); owners[i]=Shader(gpu.context.device,module);
        stages[i]={VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO}; stages[i].stage=i?VK_SHADER_STAGE_FRAGMENT_BIT:VK_SHADER_STAGE_VERTEX_BIT; stages[i].module=module; stages[i].pName="main";
    }
    VkVertexInputBindingDescription binding{0,sizeof(glm::vec3),VK_VERTEX_INPUT_RATE_VERTEX};
    VkVertexInputAttributeDescription attribute{0,0,VK_FORMAT_R32G32B32_SFLOAT,0};
    VkPipelineVertexInputStateCreateInfo vertex{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    vertex.vertexBindingDescriptionCount=vertex.vertexAttributeDescriptionCount=1; vertex.pVertexBindingDescriptions=&binding; vertex.pVertexAttributeDescriptions=&attribute;
    VkPipelineInputAssemblyStateCreateInfo assembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO}; assembly.topology=VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo viewport{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO}; viewport.viewportCount=viewport.scissorCount=1;
    VkPipelineRasterizationStateCreateInfo raster{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO}; raster.polygonMode=VK_POLYGON_MODE_FILL; raster.cullMode=VK_CULL_MODE_NONE; raster.lineWidth=1;
    VkPipelineMultisampleStateCreateInfo sampling{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO}; sampling.rasterizationSamples=samples;
    VkPipelineColorBlendAttachmentState attachments[3]{}; for(auto& a:attachments) a.colorWriteMask=15;
    VkPipelineColorBlendStateCreateInfo blend{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO}; blend.attachmentCount=3; blend.pAttachments=attachments;
    VkPipelineDepthStencilStateCreateInfo depth{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO}; depth.depthTestEnable=depth.depthWriteEnable=VK_TRUE; depth.depthCompareOp=VK_COMPARE_OP_LESS_OR_EQUAL;
    VkDynamicState states[]{VK_DYNAMIC_STATE_VIEWPORT,VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO}; dynamic.dynamicStateCount=2; dynamic.pDynamicStates=states;
    VkFormat formats[]{VK_FORMAT_R16G16B16A16_SFLOAT,VK_FORMAT_R16G16B16A16_SFLOAT,VK_FORMAT_R16G16B16A16_SFLOAT};
    VkPipelineRenderingCreateInfo rendering{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO}; rendering.colorAttachmentCount=3; rendering.pColorAttachmentFormats=formats; rendering.depthAttachmentFormat=VK_FORMAT_D32_SFLOAT;
    VkGraphicsPipelineCreateInfo info{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO}; info.pNext=&rendering; info.layout=layout; info.stageCount=2; info.pStages=stages;
    info.pVertexInputState=&vertex; info.pInputAssemblyState=&assembly; info.pViewportState=&viewport; info.pRasterizationState=&raster; info.pMultisampleState=&sampling;
    info.pColorBlendState=&blend; info.pDepthStencilState=&depth; info.pDynamicState=&dynamic;
    VkPipeline result{}; VK_CHECK(vkCreateGraphicsPipelines(gpu.context.device,{},1,&info,nullptr,&result)); return labut2::Pipeline(gpu.context.device,result);
}
struct Target { VkImage image; VkImageView view; };
Target TargetImage(Gpu& gpu, uint32_t size, VkFormat format) { auto& i=gpu.Image(size,format); return {i.image,i.view}; }
struct alignas(16) DrawPC { glm::mat4 mvp; glm::vec4 options; };
void RenderGeometry(Gpu& gpu, uint32_t size, const std::array<Target,3>& colors, Target depth,
    engine::MsaaTargets& msaa, VkPipeline pipeline, VkPipelineLayout layout,
    VkBuffer positions, VkBuffer indices, uint32_t indexCount, const std::vector<RenderBatch>& batches,
    const glm::mat4& vp, bool checker, const std::vector<glm::mat4>& bones = {}) {
    VkRenderingAttachmentInfo attachments[3]{};
    for(size_t i=0;i<3;++i) {
        gpu.Barrier(colors[i].image,VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        attachments[i]={VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO}; attachments[i].imageView=colors[i].view;
        attachments[i].imageLayout=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL; attachments[i].loadOp=VK_ATTACHMENT_LOAD_OP_CLEAR; attachments[i].storeOp=VK_ATTACHMENT_STORE_OP_STORE;
    }
    gpu.Barrier(depth.image,VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,1,1,VK_IMAGE_ASPECT_DEPTH_BIT);
    VkRenderingAttachmentInfo d{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO}; d.imageView=depth.view; d.imageLayout=VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    d.loadOp=VK_ATTACHMENT_LOAD_OP_CLEAR; d.storeOp=VK_ATTACHMENT_STORE_OP_STORE; d.clearValue.depthStencil={1,0};
    msaa.Attach(gpu.command,attachments,d);
    VkRenderingInfo pass{VK_STRUCTURE_TYPE_RENDERING_INFO}; pass.renderArea.extent={size,size}; pass.layerCount=1; pass.colorAttachmentCount=3; pass.pColorAttachments=attachments; pass.pDepthAttachment=&d;
    vkCmdBeginRendering(gpu.command,&pass);
    const VkViewport viewport{0,0,float(size),float(size),0,1}; const VkRect2D scissor{{0,0},{size,size}};
    vkCmdSetViewport(gpu.command,0,1,&viewport); vkCmdSetScissor(gpu.command,0,1,&scissor); vkCmdBindPipeline(gpu.command,VK_PIPELINE_BIND_POINT_GRAPHICS,pipeline);
    const VkDeviceSize zero=0; vkCmdBindVertexBuffers(gpu.command,0,1,&positions,&zero); vkCmdBindIndexBuffer(gpu.command,indices,0,VK_INDEX_TYPE_UINT32);
    for(const auto& batch:batches) {
        glm::mat4 skin = batch.isSkinned ? bones.at(batch.boneBaseIndex) : glm::mat4(1);
        const DrawPC pc{vp*batch.transform*skin, {checker?1.f:0.f,float(batch.materialIndex),0,0}};
        vkCmdPushConstants(gpu.command,layout,VK_SHADER_STAGE_VERTEX_BIT|VK_SHADER_STAGE_FRAGMENT_BIT,0,sizeof(pc),&pc);
        vkCmdDrawIndexed(gpu.command,indexCount,1,0,0,0);
    }
    vkCmdEndRendering(gpu.command);
}
void Tests(Gpu& gpu) {
    using namespace engine;
    Require(SelectMsaaSamples(8,1|2|4)==4,"Unsupported MSAA falls back to a supported level");
    Require(SelectMsaaSamples(3,1|2|4)==2,"Invalid sample count cannot reach Vulkan");
    for(uint32_t i=0;i<16;++i) { auto j=TemporalJitter(i); Require(glm::all(glm::lessThan(glm::abs(j),glm::vec2(0.501f))),"Jitter is bounded"); Require(TemporalJitter(i)==TemporalJitter(i+16),"Jitter repeats deterministically"); }
    gpu.images.reserve(16); gpu.buffers.reserve(64);
    constexpr uint32_t size=64;
    const std::array<Target,3> colors{TargetImage(gpu,size,VK_FORMAT_R16G16B16A16_SFLOAT),TargetImage(gpu,size,VK_FORMAT_R16G16B16A16_SFLOAT),TargetImage(gpu,size,VK_FORMAT_R16G16B16A16_SFLOAT)};
    const Target depth=TargetImage(gpu,size,VK_FORMAT_D32_SFLOAT);
    const glm::vec3 triangle[]{{-.8f,-.8f,.5f},{.8f,-.8f,.5f},{-.8f,.8f,.5f}};
    const uint32_t triangleIndices[]{0,1,2};
    const VkBuffer triangleVB=gpu.Buffer(sizeof(triangle),VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,triangle).buffer;
    const VkBuffer triangleIB=gpu.Buffer(sizeof(triangleIndices),VK_BUFFER_USAGE_INDEX_BUFFER_BIT,triangleIndices).buffer;
    VkPushConstantRange range{VK_SHADER_STAGE_VERTEX_BIT|VK_SHADER_STAGE_FRAGMENT_BIT,0,sizeof(DrawPC)};
    VkPipelineLayoutCreateInfo li{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO}; li.pushConstantRangeCount=1; li.pPushConstantRanges=&range;
    VkPipelineLayout rawLayout{}; VK_CHECK(vkCreatePipelineLayout(gpu.context.device,&li,nullptr,&rawLayout));
    labut2::PipelineLayout layout(gpu.context.device,rawLayout);
    MsaaTargets msaa; const auto support=QueryMsaaSupport(gpu.context.physicalDevice);
    RenderBatch triangleBatch{}; triangleBatch.meshIndex=0; triangleBatch.materialIndex=0; triangleBatch.entityId=1; triangleBatch.transform=glm::mat4(1);
    for(uint32_t samples:{1u,2u,4u,8u}) if(support.sampleCounts&samples) {
        msaa.Configure(gpu.context,gpu.allocator,{size,size},samples,support.depthResolve);
        auto pipeline=GeometryPipeline(gpu,layout.handle,msaa.samples);
        gpu.Begin(); RenderGeometry(gpu,size,colors,depth,msaa,pipeline.handle,layout.handle,triangleVB,triangleIB,3,{triangleBatch},glm::mat4(1),false);
        for(const auto& color:colors) gpu.Barrier(color.image,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        gpu.Submit(); auto pixels=gpu.Read(colors[0].image,size); size_t partial=0;
        for(auto p:pixels) if(p[0]>0.01f&&p[0]<0.99f) ++partial;
        Require(samples==1 ? partial==0 : partial>0,"MSAA resolves subpixel triangle coverage");
        auto bright=gpu.Read(colors[1].image,size); auto normal=gpu.Read(colors[2].image,size);
        const size_t inside=20*size+20;
        Require(std::abs(bright[inside][0]-2)<.01f&&std::abs(normal[inside][2]-1)<.01f,"All three MRT attachments resolve");
        auto& depthRead=gpu.Buffer(size*size*sizeof(float),VK_BUFFER_USAGE_TRANSFER_DST_BIT);
        gpu.Begin(); gpu.Barrier(depth.image,VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,1,1,VK_IMAGE_ASPECT_DEPTH_BIT);
        VkBufferImageCopy copy{}; copy.imageSubresource={VK_IMAGE_ASPECT_DEPTH_BIT,0,0,1}; copy.imageExtent={size,size,1};
        vkCmdCopyImageToBuffer(gpu.command,depth.image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,depthRead.buffer,1,&copy); gpu.Submit();
        void* mapped{}; VK_CHECK(vmaMapMemory(gpu.allocator.allocator,depthRead.allocation,&mapped)); vmaInvalidateAllocation(gpu.allocator.allocator,depthRead.allocation,0,VK_WHOLE_SIZE);
        const auto* values=static_cast<float*>(mapped); Require(std::abs(values[inside]-.5f)<.001f&&values[63*size+63]==1,"Depth resolve preserves geometry and background"); vmaUnmapMemory(gpu.allocator.allocator,depthRead.allocation);
        std::cout<<"PASS MSAA "<<samples<<"x: color/MRT/depth resolve, partial pixels="<<partial<<'\n';
    }
    const glm::vec3 quad[]{{-.9f,-.9f,-2},{.9f,-.9f,-2},{.9f,.9f,-2},{-.9f,.9f,-2}};
    const uint32_t quadIndices[]{0,1,2,0,2,3};
    std::vector<labut2::Buffer> positions,indices;
    positions.push_back(std::move(gpu.Buffer(sizeof(quad),VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,quad)));
    indices.push_back(std::move(gpu.Buffer(sizeof(quadIndices),VK_BUFFER_USAGE_INDEX_BUFFER_BIT,quadIndices)));
    std::unordered_map<uint32_t,labut2::Buffer> joints,weights;
    const glm::uvec4 jointData[4]{}; const glm::vec4 weightData[4]{{1,0,0,0},{1,0,0,0},{1,0,0,0},{1,0,0,0}};
    joints.emplace(0,std::move(gpu.Buffer(sizeof(jointData),VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,jointData)));
    weights.emplace(0,std::move(gpu.Buffer(sizeof(weightData),VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,weightData)));
    const std::vector<uint32_t> counts{6}; const TemporalGeometry geometry{positions,indices,counts,joints,weights};
    TemporalAntiAliasing taa; taa.Initialize(gpu.context,gpu.shaders.c_str()); taa.Resize(gpu.context,gpu.allocator,{size,size},2);
    RenderSettings settings; settings.taaEnabled=true;
    const glm::mat4 projection=glm::perspectiveRH_ZO(glm::radians(60.f),1.f,.1f,10.f);
    uint32_t frame=0;
    auto render=[&](std::vector<RenderBatch> batches,const std::vector<glm::mat4>& bones=std::vector<glm::mat4>{}) {
        taa.BeginFrame(settings,glm::mat4(1),projection,glm::vec2(size),&gpu,true);
        glm::mat4 jitter(1); jitter[3][0]=2*taa.Jitter().x/size; jitter[3][1]=2*taa.Jitter().y/size;
        const glm::mat4 p=jitter*projection; std::vector<RenderBatch> opaque,skin;
        for(auto b:batches) (b.isSkinned?skin:opaque).push_back(b);
        taa.Prepare(gpu.context,gpu.allocator,frame%2,p,p,opaque,skin,bones,colors[0].view,depth.view);
        auto pipe=GeometryPipeline(gpu,layout.handle,msaa.samples);
        gpu.Begin(); RenderGeometry(gpu,size,colors,depth,msaa,pipe.handle,layout.handle,positions[0].buffer,indices[0].buffer,6,batches,p,true,bones);
        gpu.Barrier(depth.image,VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,1,1,VK_IMAGE_ASPECT_DEPTH_BIT);
        taa.Resolve(gpu.command,frame%2,colors[0].image,geometry);
        gpu.Barrier(colors[0].image,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        gpu.Submit(); taa.Submitted(); ++frame; return gpu.Read(colors[0].image,size);
    };
    const size_t center=32*size+32;
    for(uint32_t samples:{1u,4u}) if(support.sampleCounts&samples) {
        msaa.Configure(gpu.context,gpu.allocator,{size,size},samples,support.depthResolve); taa.ResetHistory();
        auto batch=triangleBatch;
        auto first=render({batch}); Require(std::abs(first[center][0]-.2f)<.01f,"First TAA frame copies current color");
        batch.materialIndex=1; auto accumulated=render({batch});
        Require(accumulated[center][0]>.2f&&accumulated[center][0]<.7f,"TAA actually accumulates reprojected history");
        taa.ResetHistory(); auto reset=render({batch}); Require(std::abs(reset[center][0]-.8f)<.01f,"Reset discards stale color history");
        taa.ResetHistory(); batch.materialIndex=0; render({batch}); batch.materialIndex=1; batch.transform[3][0]=.15f;
        auto moving=render({batch}); Require(moving[center][0]<.75f,"Rigid object motion reprojects its previous transform");
        taa.ResetHistory(); batch.transform=glm::mat4(1); batch.isSkinned=true; batch.materialIndex=0;
        render({batch},{glm::mat4(1)}); batch.materialIndex=1; glm::mat4 bone(1); bone[3][0]=.15f;
        auto animated=render({batch},{bone}); Require(animated[center][0]<.75f,"Skeletal motion reprojects previous bone matrices");
        taa.ResetHistory(); batch.isSkinned=false; batch.materialIndex=0;
        auto background=batch; background.entityId=2; background.materialIndex=1; background.transform[3][2]=-1;
        render({background,batch}); auto revealed=render({background});
        Require(std::abs(revealed[center][0]-.8f)<.01f,"Depth rejection prevents foreground ghosts on newly revealed geometry");
        std::cout<<"PASS TAA + "<<samples<<"x MSAA: first frame, accumulation, reset, object/bone motion, disocclusion\n";
    }
    glm::mat4 smoothProjection=projection; smoothProjection[0][0]*=1.001f; smoothProjection[1][1]*=1.001f;
    taa.BeginFrame(settings,glm::mat4(1),smoothProjection,glm::vec2(size),&gpu,true);
    Require(taa.HistoryValid(),"Smooth gameplay FOV changes must retain temporal history");
    glm::mat4 cut(1); cut[3][0]=20;
    taa.BeginFrame(settings,cut,projection,glm::vec2(size),&gpu,true); Require(!taa.HistoryValid(),"Camera cuts invalidate history");
    taa.BeginFrame(settings,glm::mat4(1),projection,glm::vec2(size),&gpu,false); Require(taa.Jitter()==glm::vec2(0)&&!taa.HistoryValid(),"Disabled TAA and diagnostic views have no jitter/history");
    taa.Resize(gpu.context,gpu.allocator,{32,32},2); Require(!taa.HistoryValid()&&taa.FrameNumber()==0,"Resize resets history");
    taa.Shutdown(); Require(Gpu::validationErrors==0,"No Vulkan validation errors");
}
} // namespace
int main(int argc,char** argv) {
    try { Require(argc==2,"Pass compiled shader directory"); Gpu gpu(argv[1]); Tests(gpu); std::cout<<"All anti-aliasing GPU regressions passed.\n"; return 0; }
    catch(const std::exception& e) { std::cerr<<"FAIL: "<<e.what()<<'\n'; return 1; }
}
