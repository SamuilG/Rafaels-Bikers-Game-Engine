// Executes the production debug shaders on a real Vulkan graphics queue.
// The tiny offscreen harness reproduces the debug pipeline's raster/depth/blend
// contract; it does not instantiate RenderSystem or claim to test its routing.
#include <volk/volk.h>
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
    constexpr uint32_t kSize = 64;
    constexpr VkFormat kColorFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
    constexpr VkFormat kDepthFormat = VK_FORMAT_D32_SFLOAT;
    void Require(bool condition, const std::string& message) {
        if (!condition) throw std::runtime_error(message);
    }
    void Check(VkResult result, const char* operation) {
        Require(result == VK_SUCCESS, std::string(operation) + " returned VkResult " + std::to_string(result));
    }
#define VK_CHECK(expression) Check((expression), #expression)

    struct SceneUniform {
        glm::mat4 camera{1}, projection{1}, projCam{1};
        glm::vec4 cameraPos{};
        glm::vec4 lights[16 * 4]{};
        glm::vec4 lightPos{}, lightColor{};
        uint32_t lightCount{}, renderMode{}, iblEnabled{}, pad{};
        glm::mat4 lightVP[4]{};
        glm::vec4 cascadeSplits{}, portalClipPlane{};
    };
    struct ObjectPC {
        glm::mat4 transform{1};
        glm::vec4 baseColorFactor{1}, emissiveFactor{};
        float metallicFactor{}, roughnessFactor{1}, alphaCutoff{};
        uint32_t boneBaseIndex{};
        glm::vec4 clipPlane{};
    };
    struct Vertex { glm::vec3 position; glm::vec2 uv; glm::vec3 normal{0, 0, 1}; };
    struct SkinnedVertex { Vertex vertex; glm::uvec4 joints{}; glm::vec4 weights{1, 0, 0, 0}; };
    static_assert(sizeof(Vertex) == 32);
    static_assert(sizeof(SkinnedVertex) == 64);
    static_assert(sizeof(ObjectPC) == 128 && offsetof(ObjectPC, clipPlane) == 112);
    static_assert(sizeof(SceneUniform) == 1568 && offsetof(SceneUniform, portalClipPlane) == 1552);
    struct Draw { std::array<Vertex, 6> vertices; ObjectPC pc{}; bool cutoutTexture = false; };
    using Pixel = std::array<float, 4>;
    using Frame = std::vector<Pixel>;

    Draw Quad(float firstDepth, float secondDepth = -1, float uvScale = 1, bool verticalSlope = false) {
        if (secondDepth < 0) secondDepth = firstDepth;
        const float tangent = std::tan(glm::radians(60.0f) / 2);
        const auto vertex = [&](float x, float y) {
            const float depth = (verticalSlope ? y : x) < 0 ? firstDepth : secondDepth;
            return Vertex{{x * depth * tangent, y * depth * tangent, -depth},
                {(x + 1) * 0.5f * uvScale, (y + 1) * 0.5f * uvScale}};
        };
        return Draw{{vertex(-1, -1), vertex(1, -1), vertex(1, 1),
            vertex(-1, -1), vertex(1, 1), vertex(-1, 1)}};
    }

    float HalfToFloat(uint16_t value) {
        const int exponent = (value >> 10) & 31, fraction = value & 1023;
        const float magnitude = exponent == 0 ? std::ldexp(static_cast<float>(fraction), -24)
            : exponent == 31 ? (fraction ? NAN : INFINITY)
            : std::ldexp(static_cast<float>(1024 + fraction), exponent - 25);
        return value & 0x8000 ? -magnitude : magnitude;
    }

    class Gpu {
        struct Buffer { VkBuffer handle{}; VkDeviceMemory memory{}; VkDeviceSize size{}; };
        struct Image { VkImage handle{}; VkDeviceMemory memory{}; VkImageView view{}; uint32_t levels = 1; };
        VkInstance instance{};
        VkPhysicalDevice physical{};
        VkDevice device{};
        VkQueue queue{};
        uint32_t family{};
        VkPhysicalDeviceMemoryProperties memoryProperties{};
        VkCommandPool commandPool{};
        VkCommandBuffer command{};
        std::vector<Buffer> buffers;
        std::vector<Image> images;
        std::vector<VkPipeline> pipelines;
        VkDescriptorSetLayout sceneLayout{}, materialLayout{}, boneLayout{};
        VkDescriptorPool descriptorPool{};
        VkDescriptorSet sceneSet{}, whiteSet{}, cutoutSet{}, boneSet{};
        VkPipelineLayout pipelineLayout{};
        VkSampler sampler{};
        Buffer sceneBuffer{}, vertices{}, readback{}, boneBuffer{};
        Image color{}, depth{};
        std::filesystem::path shaderDirectory;

        uint32_t MemoryType(uint32_t bits, VkMemoryPropertyFlags required) {
            for (uint32_t index = 0; index < memoryProperties.memoryTypeCount; ++index)
                if ((bits & (1u << index)) && (memoryProperties.memoryTypes[index].propertyFlags & required) == required) return index;
            throw std::runtime_error("Required Vulkan memory type is unavailable");
        }
        Buffer MakeBuffer(VkDeviceSize size, VkBufferUsageFlags usage, const void* data = nullptr) {
            Buffer result{}; result.size = size;
            VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
            info.size = size; info.usage = usage; info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
            VK_CHECK(vkCreateBuffer(device, &info, nullptr, &result.handle));
            VkMemoryRequirements requirements{}; vkGetBufferMemoryRequirements(device, result.handle, &requirements);
            VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
            allocation.allocationSize = requirements.size;
            allocation.memoryTypeIndex = MemoryType(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
            VK_CHECK(vkAllocateMemory(device, &allocation, nullptr, &result.memory));
            VK_CHECK(vkBindBufferMemory(device, result.handle, result.memory, 0));
            buffers.push_back(result);
            if (data) Write(result, data, size);
            return result;
        }
        void Write(const Buffer& buffer, const void* data, VkDeviceSize size) {
            Require(size <= buffer.size, "host upload fits buffer");
            void* mapped{}; VK_CHECK(vkMapMemory(device, buffer.memory, 0, size, 0, &mapped));
            std::memcpy(mapped, data, static_cast<size_t>(size)); vkUnmapMemory(device, buffer.memory);
        }
        Image MakeImage(VkFormat format, VkImageUsageFlags usage, VkImageAspectFlags aspect, uint32_t levels = 1) {
            Image result{}; result.levels = levels;
            VkImageCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
            info.imageType = VK_IMAGE_TYPE_2D; info.format = format; info.extent = {kSize, kSize, 1};
            info.mipLevels = levels; info.arrayLayers = 1; info.samples = VK_SAMPLE_COUNT_1_BIT;
            info.tiling = VK_IMAGE_TILING_OPTIMAL; info.usage = usage; info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
            VK_CHECK(vkCreateImage(device, &info, nullptr, &result.handle));
            VkMemoryRequirements requirements{}; vkGetImageMemoryRequirements(device, result.handle, &requirements);
            VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
            allocation.allocationSize = requirements.size;
            allocation.memoryTypeIndex = MemoryType(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
            VK_CHECK(vkAllocateMemory(device, &allocation, nullptr, &result.memory));
            VK_CHECK(vkBindImageMemory(device, result.handle, result.memory, 0));
            VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
            view.image = result.handle; view.viewType = VK_IMAGE_VIEW_TYPE_2D; view.format = format;
            view.subresourceRange = {aspect, 0, levels, 0, 1};
            VK_CHECK(vkCreateImageView(device, &view, nullptr, &result.view));
            images.push_back(result); return result;
        }
        void Begin() {
            VK_CHECK(vkResetCommandBuffer(command, 0));
            VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
            begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
            VK_CHECK(vkBeginCommandBuffer(command, &begin));
        }
        void Submit() {
            VK_CHECK(vkEndCommandBuffer(command));
            VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO}; submit.commandBufferCount = 1; submit.pCommandBuffers = &command;
            VK_CHECK(vkQueueSubmit(queue, 1, &submit, VK_NULL_HANDLE));
            VK_CHECK(vkQueueWaitIdle(queue));
        }
        void Barrier(const Image& image, VkImageAspectFlags aspect, VkImageLayout oldLayout, VkImageLayout newLayout) {
            VkImageMemoryBarrier2 barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
            barrier.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
            barrier.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT | VK_ACCESS_2_MEMORY_READ_BIT;
            barrier.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
            barrier.dstAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT | VK_ACCESS_2_MEMORY_READ_BIT;
            barrier.oldLayout = oldLayout; barrier.newLayout = newLayout;
            barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.image = image.handle; barrier.subresourceRange = {aspect, 0, image.levels, 0, 1};
            VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO}; dependency.imageMemoryBarrierCount = 1;
            dependency.pImageMemoryBarriers = &barrier; vkCmdPipelineBarrier2(command, &dependency);
        }
        Image MakeTexture(bool cutout) {
            constexpr uint32_t levels = 4; // Deliberately stop at 8x8: maximum available LOD is 3.
            Image texture = MakeImage(VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                VK_IMAGE_ASPECT_COLOR_BIT, levels);
            std::vector<uint8_t> pixels;
            std::vector<VkBufferImageCopy> regions;
            for (uint32_t level = 0; level < levels; ++level) {
                const uint32_t size = kSize >> level;
                VkBufferImageCopy copy{}; copy.bufferOffset = pixels.size();
                copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, level, 0, 1}; copy.imageExtent = {size, size, 1};
                regions.push_back(copy);
                for (uint32_t y = 0; y < size; ++y) for (uint32_t x = 0; x < size; ++x) {
                    pixels.insert(pixels.end(), {255, 255, 255, static_cast<uint8_t>(cutout && x < size / 2 ? 0 : 255)});
                }
            }
            const Buffer staging = MakeBuffer(pixels.size(), VK_BUFFER_USAGE_TRANSFER_SRC_BIT, pixels.data());
            Begin();
            Barrier(texture, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
            vkCmdCopyBufferToImage(command, staging.handle, texture.handle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                static_cast<uint32_t>(regions.size()), regions.data());
            Barrier(texture, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            Submit(); return texture;
        }
        VkDescriptorSet AllocateSet(VkDescriptorSetLayout layout) {
            VkDescriptorSetAllocateInfo allocate{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
            allocate.descriptorPool = descriptorPool; allocate.descriptorSetCount = 1; allocate.pSetLayouts = &layout;
            VkDescriptorSet set{}; VK_CHECK(vkAllocateDescriptorSets(device, &allocate, &set)); return set;
        }
        VkDescriptorSet TextureSet(const Image& image) {
            const VkDescriptorSet set = AllocateSet(materialLayout);
            VkDescriptorImageInfo info{sampler, image.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
            VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET}; write.dstSet = set;
            write.descriptorCount = 1; write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; write.pImageInfo = &info;
            vkUpdateDescriptorSets(device, 1, &write, 0, nullptr); return set;
        }
        VkShaderModule Shader(const char* filename) {
            std::ifstream stream(shaderDirectory / filename, std::ios::binary | std::ios::ate);
            Require(stream.good(), std::string("compiled production shader exists: ") + filename);
            const size_t size = static_cast<size_t>(stream.tellg());
            Require(size > 0 && size % 4 == 0, "SPIR-V file is complete");
            std::vector<uint32_t> bytes(size / 4); stream.seekg(0); stream.read(reinterpret_cast<char*>(bytes.data()), size);
            VkShaderModuleCreateInfo info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO}; info.codeSize = size; info.pCode = bytes.data();
            VkShaderModule module{}; VK_CHECK(vkCreateShaderModule(device, &info, nullptr, &module)); return module;
        }
    public:
        explicit Gpu(std::filesystem::path shaderPath) : shaderDirectory(std::move(shaderPath)) {
            VK_CHECK(volkInitialize());
            VkApplicationInfo application{VK_STRUCTURE_TYPE_APPLICATION_INFO};
            application.pApplicationName = "Steer Engine View Mode GPU Tests"; application.apiVersion = VK_API_VERSION_1_3;
            VkInstanceCreateInfo create{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO}; create.pApplicationInfo = &application;
            VK_CHECK(vkCreateInstance(&create, nullptr, &instance)); volkLoadInstance(instance);
            uint32_t count{}; VK_CHECK(vkEnumeratePhysicalDevices(instance, &count, nullptr));
            Require(count > 0, "real Vulkan physical device available");
            std::vector<VkPhysicalDevice> available(count); VK_CHECK(vkEnumeratePhysicalDevices(instance, &count, available.data()));
            physical = available.front();
            VkPhysicalDeviceProperties properties{}; vkGetPhysicalDeviceProperties(physical, &properties);
            std::printf("GPU: %s; Vulkan %u.%u.%u\n", properties.deviceName, VK_API_VERSION_MAJOR(properties.apiVersion),
                VK_API_VERSION_MINOR(properties.apiVersion), VK_API_VERSION_PATCH(properties.apiVersion));
            vkGetPhysicalDeviceMemoryProperties(physical, &memoryProperties);
            VkPhysicalDeviceScalarBlockLayoutFeatures scalar{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SCALAR_BLOCK_LAYOUT_FEATURES};
            VkPhysicalDeviceVulkan13Features features13{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES}; features13.pNext = &scalar;
            VkPhysicalDeviceFeatures2 features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2}; features.pNext = &features13;
            vkGetPhysicalDeviceFeatures2(physical, &features);
            Require(features13.dynamicRendering && features13.synchronization2 && scalar.scalarBlockLayout,
                "GPU supports production dynamic rendering, synchronization2 and scalar layout");
            features13 = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES}; features13.pNext = &scalar;
            features13.dynamicRendering = features13.synchronization2 = VK_TRUE;
            uint32_t queueCount{}; vkGetPhysicalDeviceQueueFamilyProperties(physical, &queueCount, nullptr);
            std::vector<VkQueueFamilyProperties> queues(queueCount); vkGetPhysicalDeviceQueueFamilyProperties(physical, &queueCount, queues.data());
            while (family < queueCount && !(queues[family].queueFlags & VK_QUEUE_GRAPHICS_BIT)) ++family;
            Require(family < queueCount, "graphics queue exists");
            float priority = 1;
            VkDeviceQueueCreateInfo queueInfo{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
            queueInfo.queueFamilyIndex = family; queueInfo.queueCount = 1; queueInfo.pQueuePriorities = &priority;
            VkDeviceCreateInfo deviceInfo{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO}; deviceInfo.pNext = &features13;
            deviceInfo.queueCreateInfoCount = 1; deviceInfo.pQueueCreateInfos = &queueInfo;
            VK_CHECK(vkCreateDevice(physical, &deviceInfo, nullptr, &device)); volkLoadDevice(device); vkGetDeviceQueue(device, family, 0, &queue);
            VkCommandPoolCreateInfo pool{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO}; pool.queueFamilyIndex = family;
            pool.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT; VK_CHECK(vkCreateCommandPool(device, &pool, nullptr, &commandPool));
            VkCommandBufferAllocateInfo allocate{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO}; allocate.commandPool = commandPool;
            allocate.commandBufferCount = 1; allocate.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            VK_CHECK(vkAllocateCommandBuffers(device, &allocate, &command));
            color = MakeImage(kColorFormat, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
            depth = MakeImage(kDepthFormat, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, VK_IMAGE_ASPECT_DEPTH_BIT);
            sceneBuffer = MakeBuffer(sizeof(SceneUniform), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
            vertices = MakeBuffer(sizeof(SkinnedVertex) * 6 * 64, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
            boneBuffer = MakeBuffer(sizeof(glm::mat4) * 2, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
            readback = MakeBuffer(kSize * kSize * 8, VK_BUFFER_USAGE_TRANSFER_DST_BIT);
            VkDescriptorSetLayoutBinding sceneBinding{0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
            VkDescriptorSetLayoutCreateInfo layout{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO}; layout.bindingCount = 1; layout.pBindings = &sceneBinding;
            VK_CHECK(vkCreateDescriptorSetLayout(device, &layout, nullptr, &sceneLayout));
            VkDescriptorSetLayoutBinding materialBinding{0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
            layout.pBindings = &materialBinding; VK_CHECK(vkCreateDescriptorSetLayout(device, &layout, nullptr, &materialLayout));
            VkDescriptorSetLayoutBinding boneBinding{0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT, nullptr};
            layout.pBindings = &boneBinding; VK_CHECK(vkCreateDescriptorSetLayout(device, &layout, nullptr, &boneLayout));
            const VkDescriptorPoolSize sizes[] = {{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1}, {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2}, {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1}};
            VkDescriptorPoolCreateInfo descriptors{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO}; descriptors.maxSets = 4;
            descriptors.poolSizeCount = 3; descriptors.pPoolSizes = sizes;
            VK_CHECK(vkCreateDescriptorPool(device, &descriptors, nullptr, &descriptorPool));
            sceneSet = AllocateSet(sceneLayout);
            VkDescriptorBufferInfo bufferInfo{sceneBuffer.handle, 0, sizeof(SceneUniform)};
            VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET}; write.dstSet = sceneSet;
            write.descriptorCount = 1; write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER; write.pBufferInfo = &bufferInfo;
            vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
            boneSet = AllocateSet(boneLayout);
            VkDescriptorBufferInfo boneInfo{boneBuffer.handle, 0, sizeof(glm::mat4) * 2};
            write.dstSet = boneSet; write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; write.pBufferInfo = &boneInfo;
            vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
            VkSamplerCreateInfo sampling{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO}; sampling.magFilter = sampling.minFilter = VK_FILTER_LINEAR;
            sampling.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
            sampling.addressModeU = sampling.addressModeV = sampling.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
            sampling.maxLod = 16; sampling.maxAnisotropy = 1;
            VK_CHECK(vkCreateSampler(device, &sampling, nullptr, &sampler));
            whiteSet = TextureSet(MakeTexture(false)); cutoutSet = TextureSet(MakeTexture(true));
            const VkDescriptorSetLayout layouts[] = {sceneLayout, materialLayout, boneLayout};
            VkPushConstantRange push{VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(ObjectPC)};
            VkPipelineLayoutCreateInfo pipeline{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
            pipeline.setLayoutCount = 3; pipeline.pSetLayouts = layouts; pipeline.pushConstantRangeCount = 1; pipeline.pPushConstantRanges = &push;
            VK_CHECK(vkCreatePipelineLayout(device, &pipeline, nullptr, &pipelineLayout));
        }
        ~Gpu() {
            if (device) {
                vkDeviceWaitIdle(device);
                for (auto pipeline : pipelines) vkDestroyPipeline(device, pipeline, nullptr);
                vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
                vkDestroyDescriptorPool(device, descriptorPool, nullptr);
                vkDestroyDescriptorSetLayout(device, sceneLayout, nullptr); vkDestroyDescriptorSetLayout(device, materialLayout, nullptr);
                vkDestroyDescriptorSetLayout(device, boneLayout, nullptr);
                vkDestroySampler(device, sampler, nullptr);
                for (const auto& image : images) { vkDestroyImageView(device, image.view, nullptr); vkDestroyImage(device, image.handle, nullptr); vkFreeMemory(device, image.memory, nullptr); }
                for (const auto& buffer : buffers) { vkDestroyBuffer(device, buffer.handle, nullptr); vkFreeMemory(device, buffer.memory, nullptr); }
                vkDestroyCommandPool(device, commandPool, nullptr); vkDestroyDevice(device, nullptr);
            }
            if (instance) vkDestroyInstance(instance, nullptr);
        }
        VkPipeline Pipeline(const char* fragment, bool accumulate = false, bool depthTest = true, bool skinned = false) {
            const VkShaderModule vertex = Shader(skinned ? "skinned.vert.spv" : "debug.vert.spv"), frag = Shader(fragment);
            VkPipelineShaderStageCreateInfo stages[2]{};
            stages[0] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO}; stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT; stages[0].module = vertex; stages[0].pName = "main";
            stages[1] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO}; stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT; stages[1].module = frag; stages[1].pName = "main";
            VkVertexInputBindingDescription binding{0, skinned ? sizeof(SkinnedVertex) : sizeof(Vertex), VK_VERTEX_INPUT_RATE_VERTEX};
            const VkVertexInputAttributeDescription attributes[] = {{0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, position)},
                {1, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex, uv)}, {2, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, normal)},
                {3, 0, VK_FORMAT_R32G32B32A32_UINT, offsetof(SkinnedVertex, joints)}, {4, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(SkinnedVertex, weights)}};
            VkPipelineVertexInputStateCreateInfo input{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
            input.vertexBindingDescriptionCount = 1; input.pVertexBindingDescriptions = &binding;
            input.vertexAttributeDescriptionCount = skinned ? 5 : 3; input.pVertexAttributeDescriptions = attributes;
            VkPipelineInputAssemblyStateCreateInfo assembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO}; assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
            VkPipelineViewportStateCreateInfo viewport{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO}; viewport.viewportCount = viewport.scissorCount = 1;
            VkPipelineRasterizationStateCreateInfo raster{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
            raster.polygonMode = VK_POLYGON_MODE_FILL; raster.cullMode = VK_CULL_MODE_NONE; raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE; raster.lineWidth = 1;
            VkPipelineMultisampleStateCreateInfo samples{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO}; samples.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
            VkPipelineDepthStencilStateCreateInfo depthState{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
            depthState.depthTestEnable = depthState.depthWriteEnable = depthTest; depthState.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
            VkPipelineColorBlendAttachmentState attachment{}; attachment.blendEnable = accumulate;
            attachment.srcColorBlendFactor = attachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE; attachment.colorBlendOp = VK_BLEND_OP_ADD;
            attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE; attachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO; attachment.alphaBlendOp = VK_BLEND_OP_ADD;
            attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
            VkPipelineColorBlendStateCreateInfo blend{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO}; blend.attachmentCount = 1; blend.pAttachments = &attachment;
            const VkDynamicState dynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
            VkPipelineDynamicStateCreateInfo dynamic{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO}; dynamic.dynamicStateCount = 2; dynamic.pDynamicStates = dynamicStates;
            VkPipelineRenderingCreateInfo rendering{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
            rendering.colorAttachmentCount = 1; rendering.pColorAttachmentFormats = &kColorFormat; rendering.depthAttachmentFormat = kDepthFormat;
            VkGraphicsPipelineCreateInfo pipeline{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO}; pipeline.pNext = &rendering;
            pipeline.stageCount = 2; pipeline.pStages = stages; pipeline.pVertexInputState = &input; pipeline.pInputAssemblyState = &assembly;
            pipeline.pViewportState = &viewport; pipeline.pRasterizationState = &raster; pipeline.pMultisampleState = &samples;
            pipeline.pDepthStencilState = &depthState; pipeline.pColorBlendState = &blend; pipeline.pDynamicState = &dynamic; pipeline.layout = pipelineLayout;
            VkPipeline result{}; const VkResult status = vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipeline, nullptr, &result);
            vkDestroyShaderModule(device, vertex, nullptr); vkDestroyShaderModule(device, frag, nullptr); Check(status, "vkCreateGraphicsPipelines");
            pipelines.push_back(result); return result;
        }
        Frame Render(VkPipeline pipeline, const std::vector<Draw>& draws, glm::vec4 portalClip = {}, const glm::mat4* animatedBone = nullptr) {
            Require(!draws.empty() && draws.size() <= 64, "draw count fits vertex buffer");
            SceneUniform scene{}; scene.projection = glm::perspective(glm::radians(60.0f), 1.0f, 0.1f, 100.0f);
            scene.projection[1][1] *= -1; scene.projCam = scene.projection; scene.portalClipPlane = portalClip;
            Write(sceneBuffer, &scene, sizeof(scene));
            if (animatedBone) {
                std::vector<SkinnedVertex> allVertices;
                for (const auto& draw : draws) for (const auto& vertex : draw.vertices) allVertices.push_back({vertex});
                Write(vertices, allVertices.data(), allVertices.size() * sizeof(SkinnedVertex));
            } else {
                std::vector<Vertex> allVertices; for (const auto& draw : draws) allVertices.insert(allVertices.end(), draw.vertices.begin(), draw.vertices.end());
                Write(vertices, allVertices.data(), allVertices.size() * sizeof(Vertex));
            }
            const glm::mat4 bones[] = {glm::mat4(1), animatedBone ? *animatedBone : glm::mat4(1)};
            Write(boneBuffer, bones, sizeof(bones));
            Begin();
            Barrier(color, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
            Barrier(depth, VK_IMAGE_ASPECT_DEPTH_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);
            VkRenderingAttachmentInfo colorAttachment{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
            colorAttachment.imageView = color.view; colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR; colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            VkRenderingAttachmentInfo depthAttachment{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
            depthAttachment.imageView = depth.view; depthAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
            depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR; depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            depthAttachment.clearValue.depthStencil.depth = 1;
            VkRenderingInfo rendering{VK_STRUCTURE_TYPE_RENDERING_INFO}; rendering.renderArea.extent = {kSize, kSize}; rendering.layerCount = 1;
            rendering.colorAttachmentCount = 1; rendering.pColorAttachments = &colorAttachment; rendering.pDepthAttachment = &depthAttachment;
            vkCmdBeginRendering(command, &rendering);
            const VkViewport viewport{0, 0, static_cast<float>(kSize), static_cast<float>(kSize), 0, 1};
            const VkRect2D scissor{{0, 0}, {kSize, kSize}};
            vkCmdSetViewport(command, 0, 1, &viewport); vkCmdSetScissor(command, 0, 1, &scissor);
            vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
            const VkDeviceSize offset = 0; vkCmdBindVertexBuffers(command, 0, 1, &vertices.handle, &offset);
            for (uint32_t index = 0; index < draws.size(); ++index) {
                const VkDescriptorSet sets[] = {sceneSet, draws[index].cutoutTexture ? cutoutSet : whiteSet, boneSet};
                vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 3, sets, 0, nullptr);
                vkCmdPushConstants(command, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(ObjectPC), &draws[index].pc);
                vkCmdDraw(command, 6, 1, index * 6, 0);
            }
            vkCmdEndRendering(command);
            Barrier(color, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
            VkBufferImageCopy copy{}; copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}; copy.imageExtent = {kSize, kSize, 1};
            vkCmdCopyImageToBuffer(command, color.handle, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readback.handle, 1, &copy);
            VkMemoryBarrier2 host{VK_STRUCTURE_TYPE_MEMORY_BARRIER_2}; host.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT; host.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
            host.dstStageMask = VK_PIPELINE_STAGE_2_HOST_BIT; host.dstAccessMask = VK_ACCESS_2_HOST_READ_BIT;
            VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO}; dependency.memoryBarrierCount = 1; dependency.pMemoryBarriers = &host; vkCmdPipelineBarrier2(command, &dependency);
            Submit();
            void* mapped{}; VK_CHECK(vkMapMemory(device, readback.memory, 0, readback.size, 0, &mapped));
            Frame pixels(kSize * kSize); const auto* half = static_cast<const uint16_t*>(mapped);
            for (size_t pixel = 0; pixel < pixels.size(); ++pixel) for (size_t channel = 0; channel < 4; ++channel) pixels[pixel][channel] = HalfToFloat(half[pixel * 4 + channel]);
            vkUnmapMemory(device, readback.memory);
            for (const auto& pixel : pixels) for (float component : pixel) Require(std::isfinite(component), "GPU output contains no NaN/Inf");
            return pixels;
        }
    };

    Pixel Sample(const Frame& frame, uint32_t x = 23, uint32_t y = 29) { return frame[y * kSize + x]; }
    void Near(float actual, float expected, float tolerance, const char* message) {
        Require(std::abs(actual - expected) <= tolerance, std::string(message) + " actual=" + std::to_string(actual) + " expected=" + std::to_string(expected));
    }
    void Save(const std::filesystem::path& directory, const char* filename, const Frame& frame) {
        constexpr uint32_t scale = 4;
        std::vector<uint8_t> pixels(kSize * scale * kSize * scale * 3);
        for (uint32_t y = 0; y < kSize * scale; ++y) for (uint32_t x = 0; x < kSize * scale; ++x) for (uint32_t channel = 0; channel < 3; ++channel) {
            const float linear = std::clamp(frame[(y / scale) * kSize + x / scale][channel], 0.0f, 1.0f);
            const float srgb = linear <= 0.0031308f ? linear * 12.92f : 1.055f * std::pow(linear, 1 / 2.4f) - 0.055f;
            pixels[(y * kSize * scale + x) * 3 + channel] = static_cast<uint8_t>(srgb * 255 + 0.5f);
        }
        const auto path = directory / filename;
        Require(stbi_write_png(path.string().c_str(), kSize * scale, kSize * scale, 3, pixels.data(), kSize * scale * 3) != 0, "saved GPU readback PNG");
    }
}

int main(int argc, char** argv) {
    try {
        Require(argc == 3, "Usage: ViewModeGpuTests.exe <compiled production shader directory> <output directory>");
        const std::filesystem::path output = argv[2]; std::filesystem::create_directories(output);
        Gpu gpu(argv[1]);
        const VkPipeline mip = gpu.Pipeline("debug_mip.frag.spv");
        const VkPipeline depth = gpu.Pipeline("debug_depth.frag.spv");
        const VkPipeline derivatives = gpu.Pipeline("debug_deriv.frag.spv");
        const VkPipeline overdraw = gpu.Pipeline("overdraw.frag.spv", true, false);
        const VkPipeline overshading = gpu.Pipeline("overdraw.frag.spv", true, true);
        const Frame mip0 = gpu.Render(mip, {Quad(2, -1, 0.125f)}), mip1 = gpu.Render(mip, {Quad(2, -1, 2)}), mip2 = gpu.Render(mip, {Quad(2, -1, 4)});
        Near(Sample(mip0)[0], 1, 0.02f, "magnification clamps to red mip0"); Near(Sample(mip0)[1], 0, 0.02f, "mip0 has no green");
        Near(Sample(mip1)[1], 1, 0.04f, "2 texels/pixel selects green mip1"); Near(Sample(mip1)[0], 0, 0.04f, "mip1 differs from mip0");
        Near(Sample(mip2)[2], 1, 0.04f, "4 texels/pixel selects blue mip2");
        const Frame maximumMip = gpu.Render(mip, {Quad(2, -1, 1024)});
        Near(Sample(maximumMip)[0], 1, 0.02f, "LOD clamps to available mip3 red channel");
        Near(Sample(maximumMip)[1], 1, 0.02f, "LOD clamps to available mip3 yellow"); Near(Sample(maximumMip)[2], 0, 0.02f, "LOD cannot sample nonexistent levels");
        Save(output, "mip0.png", mip0); Save(output, "mip1.png", mip1); Save(output, "mip2.png", mip2);
        std::puts("PASS mipmap magnification, distinct real LODs and available-level clamp");

        const Frame nearDepth = gpu.Render(depth, {Quad(1)}), farDepth = gpu.Render(depth, {Quad(10)});
        Near(Sample(nearDepth)[0], 1.0f / 3, 0.01f, "near depth follows camera projection/log range");
        Near(Sample(farDepth)[0], 2.0f / 3, 0.01f, "far depth follows camera projection/log range");
        for (const auto& draws : {std::vector<Draw>{Quad(1), Quad(10)}, std::vector<Draw>{Quad(10), Quad(1)}})
            Near(Sample(gpu.Render(depth, draws))[0], Sample(nearDepth)[0], 0.002f, "nearest depth occludes far geometry in either submission order");
        Save(output, "depth-near.png", nearDepth); Save(output, "depth-far.png", farDepth);
        std::puts("PASS logarithmic near/far depth and depth-tested occlusion in both draw orders");

        const Frame flat = gpu.Render(derivatives, {Quad(2)});
        const Frame slopeX = gpu.Render(derivatives, {Quad(1, 4)}), slopeY = gpu.Render(derivatives, {Quad(1, 4, 1, true)});
        Near(Sample(flat)[0], 0, 0.002f, "front-facing plane has zero horizontal depth derivative");
        Near(Sample(flat)[1], 0, 0.002f, "front-facing plane has zero vertical depth derivative");
        Require(Sample(slopeX)[0] > 0.05f && Sample(slopeX)[1] < 0.01f, "horizontal slope lights red derivative only");
        Require(Sample(slopeY)[1] > 0.05f && Sample(slopeY)[0] < 0.01f, "vertical slope lights green derivative only");
        Save(output, "derivative-x.png", slopeX); Save(output, "derivative-y.png", slopeY);
        std::puts("PASS real GPU derivatives distinguish flat, horizontal and vertical slopes");

        const Frame threeLayers = gpu.Render(overdraw, {Quad(1), Quad(2), Quad(3)});
        Near(Sample(threeLayers)[0], 0.15f, 0.002f, "overdraw accumulates three overlapping fragments without depth rejection");
        const Frame frontFirst = gpu.Render(overshading, {Quad(1), Quad(2), Quad(3)});
        const Frame backFirst = gpu.Render(overshading, {Quad(3), Quad(2), Quad(1)});
        Near(Sample(frontFirst)[0], 0.05f, 0.002f, "overshading counts only front layer when near drawn first");
        Near(Sample(backFirst)[0], 0.15f, 0.002f, "overshading accumulates successive passing depth tests when far drawn first");
        Save(output, "overdraw-three-layers.png", threeLayers); Save(output, "overshading-front-first.png", frontFirst);
        std::puts("PASS additive overdraw and order-dependent depth-passing overshading");

        Draw cutout = Quad(1); cutout.cutoutTexture = true; cutout.pc.alphaCutoff = 0.5f;
        const Frame masked = gpu.Render(depth, {Quad(10), cutout});
        Near(Sample(masked, 16)[0], Sample(farDepth)[0], 0.002f, "transparent cutout leaves the farther surface visible");
        Near(Sample(masked, 48)[0], Sample(nearDepth)[0], 0.002f, "opaque half of cutout writes near depth");
        Draw clipped = Quad(1); clipped.pc.clipPlane = {1, 0, 0, 0};
        const Frame objectClip = gpu.Render(depth, {clipped});
        Near(Sample(objectClip, 16)[0], 0, 0.001f, "ObjectPC clip plane removes negative half-space");
        Near(Sample(objectClip, 48)[0], Sample(nearDepth)[0], 0.002f, "ObjectPC clip plane keeps positive half-space");
        const Frame portalClip = gpu.Render(depth, {Quad(1)}, {1, 0, 0, 0});
        Near(Sample(portalClip, 16)[0], 0, 0.001f, "scene portal clip uses true CPU UBO offset 1552");
        Near(Sample(portalClip, 48)[0], Sample(nearDepth)[0], 0.002f, "scene portal clip retains visible side");
        const Frame maskedAccumulation = gpu.Render(overdraw, {Quad(10), cutout});
        Near(Sample(maskedAccumulation, 16)[0], 0.05f, 0.002f, "discarded alpha adds no overdraw contribution");
        Near(Sample(maskedAccumulation, 48)[0], 0.10f, 0.002f, "opaque alpha adds overdraw contribution");
        Save(output, "alpha-cutout.png", masked); Save(output, "object-clip.png", objectClip);
        std::puts("PASS alpha discard, object clip plane, scene portal clip plane and masked accumulation");

        const VkPipeline skinnedDepth = gpu.Pipeline("debug_depth.frag.spv", false, true, true);
        Draw animated = Quad(2); animated.pc.transform = glm::scale(glm::mat4(1), glm::vec3(0.25f, 0.5f, 1));
        animated.pc.boneBaseIndex = 1; // Bone 0 deliberately stays identity.
        const glm::mat4 identity(1), translated = glm::translate(glm::mat4(1), glm::vec3(2, 0, 0));
        const Frame bindPose = gpu.Render(skinnedDepth, {animated}, {}, &identity);
        const Frame boneMoved = gpu.Render(skinnedDepth, {animated}, {}, &translated);
        Require(Sample(bindPose, 32)[0] > 0.4f, "skinned bind pose covers original location");
        Near(Sample(bindPose, 46)[0], 0, 0.001f, "bind pose does not cover moved location");
        Near(Sample(boneMoved, 32)[0], 0, 0.001f, "nonzero boneBaseIndex moves geometry out of original location");
        Near(Sample(boneMoved, 46)[0], Sample(bindPose, 32)[0], 0.002f, "production skinned vertex shader keeps debug depth after bone translation");
        Save(output, "skinned-bone-translation.png", boneMoved);
        std::puts("PASS production skinned.vert, bone descriptor, nonzero push-constant bone index and animated debug depth");
        std::puts("PASS all view mode shader + Vulkan GPU contract tests (no window/surface/swapchain)");
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAIL: %s\n", error.what()); return 1;
    }
}
