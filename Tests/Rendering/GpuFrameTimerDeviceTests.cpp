#include "Runtime/Rhi/GpuFrameTimer.hpp"
#include <iostream>
#include <stdexcept>
#include <string>

// This test supplies context lifetime itself, without linking the application's
// context/device selection or creating a window/surface/swapchain.
namespace labut2 {
VulkanContext::VulkanContext() = default;
VulkanContext::~VulkanContext() = default;
}

namespace {
void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
void Check(VkResult result, const char* operation) {
    if (result != VK_SUCCESS)
        throw std::runtime_error(std::string(operation) + " returned " + std::to_string(result));
}
#define VK_CHECK(expression) Check((expression), #expression)

class DeviceTest {
public:
    labut2::VulkanContext context;
    labut2::GpuFrameTimer timer;
    VkQueue queue = VK_NULL_HANDLE;
    VkCommandPool commandPool = VK_NULL_HANDLE;
    VkCommandBuffer command = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    static constexpr VkDeviceSize bufferSize = 8 * 1024 * 1024;

    ~DeviceTest() {
        if (context.device) {
            vkDeviceWaitIdle(context.device);
            timer.Shutdown();
            if (buffer) vkDestroyBuffer(context.device, buffer, nullptr);
            if (memory) vkFreeMemory(context.device, memory, nullptr);
            if (fence) vkDestroyFence(context.device, fence, nullptr);
            if (commandPool) vkDestroyCommandPool(context.device, commandPool, nullptr);
            vkDestroyDevice(context.device, nullptr);
        }
        if (context.instance) vkDestroyInstance(context.instance, nullptr);
    }

    void Initialize() {
        VK_CHECK(volkInitialize());
        VkApplicationInfo application{VK_STRUCTURE_TYPE_APPLICATION_INFO};
        application.pApplicationName = "Steer GPU frame timer regression";
        application.apiVersion = VK_API_VERSION_1_0;
        VkInstanceCreateInfo instanceInfo{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
        instanceInfo.pApplicationInfo = &application;
        VK_CHECK(vkCreateInstance(&instanceInfo, nullptr, &context.instance));
        volkLoadInstance(context.instance);
        std::uint32_t count = 0;
        VK_CHECK(vkEnumeratePhysicalDevices(context.instance, &count, nullptr));
        std::vector<VkPhysicalDevice> devices(count);
        VK_CHECK(vkEnumeratePhysicalDevices(context.instance, &count, devices.data()));
        for (auto device : devices) {
            std::uint32_t familyCount = 0;
            vkGetPhysicalDeviceQueueFamilyProperties(device, &familyCount, nullptr);
            std::vector<VkQueueFamilyProperties> families(familyCount);
            vkGetPhysicalDeviceQueueFamilyProperties(device, &familyCount, families.data());
            for (std::uint32_t index = 0; index < familyCount; ++index) {
                if (families[index].queueCount && (families[index].queueFlags & VK_QUEUE_GRAPHICS_BIT) &&
                    families[index].timestampValidBits) {
                    context.physicalDevice = device;
                    context.graphicsFamilyIndex = index;
                    break;
                }
            }
            if (context.physicalDevice) break;
        }
        Require(context.physicalDevice != VK_NULL_HANDLE, "No graphics queue supporting timestamps is available.");
        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties(context.physicalDevice, &properties);
        std::cout << "Device: " << properties.deviceName << "; timestamp period: "
            << properties.limits.timestampPeriod << " ns\n";
        const float priority = 1.0f;
        VkDeviceQueueCreateInfo queueInfo{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
        queueInfo.queueFamilyIndex = context.graphicsFamilyIndex;
        queueInfo.queueCount = 1;
        queueInfo.pQueuePriorities = &priority;
        VkDeviceCreateInfo deviceInfo{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
        deviceInfo.queueCreateInfoCount = 1;
        deviceInfo.pQueueCreateInfos = &queueInfo;
        VK_CHECK(vkCreateDevice(context.physicalDevice, &deviceInfo, nullptr, &context.device));
        volkLoadDevice(context.device);
        vkGetDeviceQueue(context.device, context.graphicsFamilyIndex, 0, &queue);
        Require(timer.Initialize(context, 2), "Production timer failed to create timestamp queries.");

        VkCommandPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        poolInfo.queueFamilyIndex = context.graphicsFamilyIndex;
        poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        VK_CHECK(vkCreateCommandPool(context.device, &poolInfo, nullptr, &commandPool));
        VkCommandBufferAllocateInfo commandInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        commandInfo.commandPool = commandPool;
        commandInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        commandInfo.commandBufferCount = 1;
        VK_CHECK(vkAllocateCommandBuffers(context.device, &commandInfo, &command));
        VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        VK_CHECK(vkCreateFence(context.device, &fenceInfo, nullptr, &fence));
        VkBufferCreateInfo bufferInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        bufferInfo.size = bufferSize;
        bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        VK_CHECK(vkCreateBuffer(context.device, &bufferInfo, nullptr, &buffer));
        VkMemoryRequirements requirements{};
        vkGetBufferMemoryRequirements(context.device, buffer, &requirements);
        VkPhysicalDeviceMemoryProperties memoryProperties{};
        vkGetPhysicalDeviceMemoryProperties(context.physicalDevice, &memoryProperties);
        std::optional<std::uint32_t> memoryType;
        for (std::uint32_t index = 0; index < memoryProperties.memoryTypeCount; ++index)
            if (requirements.memoryTypeBits & (1u << index)) { memoryType = index; break; }
        Require(memoryType.has_value(), "No memory type can back the test buffer.");
        VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        allocation.allocationSize = requirements.size;
        allocation.memoryTypeIndex = *memoryType;
        VK_CHECK(vkAllocateMemory(context.device, &allocation, nullptr, &memory));
        VK_CHECK(vkBindBufferMemory(context.device, buffer, memory, 0));
    }

    void Submit(std::uint32_t slot) {
        VK_CHECK(vkResetFences(context.device, 1, &fence));
        VK_CHECK(vkResetCommandBuffer(command, 0));
        VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        VK_CHECK(vkBeginCommandBuffer(command, &begin));
        timer.Begin(command, slot);
        for (std::uint32_t pass = 0; pass < 8; ++pass) {
            VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT,
                VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1, &barrier, 0, nullptr, 0, nullptr);
            vkCmdFillBuffer(command, buffer, 0, bufferSize, 0x12340000u + pass);
        }
        timer.End(command, slot);
        VK_CHECK(vkEndCommandBuffer(command));
        VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        submit.commandBufferCount = 1;
        submit.pCommandBuffers = &command;
        VK_CHECK(vkQueueSubmit(queue, 1, &submit, fence));
        timer.Submitted(slot);
        // The harness waits for deterministic verification; the timer itself
        // only reads after this existing completion fence and never waits.
        VK_CHECK(vkWaitForFences(context.device, 1, &fence, VK_TRUE, 5000000000ULL));
    }
};
}

int main() try {
    DeviceTest test;
    test.Initialize();
    for (std::uint32_t frame = 0; frame < 3; ++frame) {
        const auto slot = frame % 2;
        Require(!test.timer.ConsumeCompleted(slot), "An idle slot must not return a previous sample.");
        test.Submit(slot);
        const auto duration = test.timer.ConsumeCompleted(slot);
        Require(duration && std::isfinite(*duration) && *duration > 0.0,
            "GPU timestamp queries must yield a finite positive duration after fence completion.");
        Require(!test.timer.ConsumeCompleted(slot), "Actual device sample must also be delivered only once.");
        std::cout << "[PASS] GPU frame " << frame << ", slot " << slot << ": " << *duration << " ms\n";
    }
    test.Submit(1);
    test.timer.DiscardPending();
    Require(!test.timer.ConsumeCompleted(1), "Discard must suppress an otherwise valid completed device sample.");
    std::cout << "[PASS] Completed GPU sample discard.\n4 headless GPU timer device regression groups passed.\n";
    return 0;
}
catch (const std::exception& error) {
    std::cerr << "[FAIL] " << error.what() << '\n';
    return 1;
}
