#include "Runtime/Rhi/GpuFrameTimer.hpp"
#include <cstring>
#include <iostream>
#include <stdexcept>

// Only the timer is under test. This isolated executable supplies a non-owning
// context and mocked Volk entry points; it does not create a Vulkan device.
namespace labut2 {
VulkanContext::VulkanContext() = default;
VulkanContext::~VulkanContext() = default;
}

namespace {
void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
void RequireMs(std::optional<double> value, double expected, const char* message) {
    Require(value && std::abs(*value - expected) < 0.000000001, message);
}

struct ApiState {
    std::uint32_t familyCount = 1;
    std::uint32_t validBits = 32;
    float period = 100.0f;
    VkResult createResult = VK_SUCCESS;
    VkResult readResult = VK_SUCCESS;
    std::uint64_t begin = 100, end = 10100;
    std::uint64_t beginAvailable = 1, endAvailable = 1;
    unsigned creates = 0, destroys = 0, reads = 0, resets = 0;
    std::uint32_t queryCount = 0, readFirst = 0, resetFirst = 0;
    VkQueryResultFlags readFlags = 0;
    std::vector<std::pair<VkPipelineStageFlagBits, std::uint32_t>> writes;
} api;

VkQueryPool Pool() { return reinterpret_cast<VkQueryPool>(std::uintptr_t{123}); }
VKAPI_ATTR void VKAPI_CALL Families(VkPhysicalDevice, std::uint32_t* count,
    VkQueueFamilyProperties* families) {
    if (families && *count) {
        families[0] = {};
        families[0].queueFlags = VK_QUEUE_GRAPHICS_BIT;
        families[0].timestampValidBits = api.validBits;
    }
    *count = api.familyCount;
}
VKAPI_ATTR void VKAPI_CALL Properties(VkPhysicalDevice, VkPhysicalDeviceProperties* properties) {
    *properties = {};
    properties->limits.timestampPeriod = api.period;
}
VKAPI_ATTR VkResult VKAPI_CALL Create(VkDevice, const VkQueryPoolCreateInfo* info,
    const VkAllocationCallbacks*, VkQueryPool* pool) {
    Require(info->sType == VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO &&
        info->queryType == VK_QUERY_TYPE_TIMESTAMP, "Only a timestamp query pool is valid.");
    ++api.creates;
    api.queryCount = info->queryCount;
    *pool = Pool();
    return api.createResult;
}
VKAPI_ATTR void VKAPI_CALL Destroy(VkDevice, VkQueryPool pool, const VkAllocationCallbacks*) {
    Require(pool == Pool(), "Shutdown must destroy the owned pool.");
    ++api.destroys;
}
VKAPI_ATTR void VKAPI_CALL Reset(VkCommandBuffer, VkQueryPool pool,
    std::uint32_t first, std::uint32_t count) {
    Require(pool == Pool() && count == 2, "Each slot must reset exactly its own two queries.");
    ++api.resets;
    api.resetFirst = first;
}
VKAPI_ATTR void VKAPI_CALL Write(VkCommandBuffer, VkPipelineStageFlagBits stage,
    VkQueryPool pool, std::uint32_t query) {
    Require(pool == Pool(), "Timestamps must target the timer's pool.");
    api.writes.emplace_back(stage, query);
}
VKAPI_ATTR VkResult VKAPI_CALL Read(VkDevice, VkQueryPool pool, std::uint32_t first,
    std::uint32_t count, std::size_t size, void* data, VkDeviceSize stride,
    VkQueryResultFlags flags) {
    Require(pool == Pool() && count == 2 && size == 4 * sizeof(std::uint64_t) &&
        stride == 2 * sizeof(std::uint64_t), "Query read must include two value/availability pairs.");
    Require((flags & VK_QUERY_RESULT_WAIT_BIT) == 0, "Profiling must never add a GPU wait.");
    Require(flags == (VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WITH_AVAILABILITY_BIT),
        "Query results must retain 64-bit timestamps and explicit availability.");
    ++api.reads;
    api.readFirst = first;
    api.readFlags = flags;
    const std::uint64_t values[]{api.begin, api.beginAvailable, api.end, api.endAvailable};
    std::memcpy(data, values, sizeof(values));
    return api.readResult;
}
}

extern "C" {
PFN_vkGetPhysicalDeviceQueueFamilyProperties vkGetPhysicalDeviceQueueFamilyProperties = Families;
PFN_vkGetPhysicalDeviceProperties vkGetPhysicalDeviceProperties = Properties;
PFN_vkCreateQueryPool vkCreateQueryPool = Create;
PFN_vkDestroyQueryPool vkDestroyQueryPool = Destroy;
PFN_vkCmdResetQueryPool vkCmdResetQueryPool = Reset;
PFN_vkCmdWriteTimestamp vkCmdWriteTimestamp = Write;
PFN_vkGetQueryPoolResults vkGetQueryPoolResults = Read;
}

int main() try {
    const auto max = std::numeric_limits<std::uint64_t>::max();
    RequireMs(labut2::GpuTimestampMilliseconds(0xfffffff0ULL, 0x10, 32, 1000), 0.032,
        "32-bit timestamps must wrap at the queue's valid width.");
    RequireMs(labut2::GpuTimestampMilliseconds(max - 15, 16, 64, 1000), 0.032,
        "64-bit timestamps must wrap without an invalid 64-bit shift.");
    RequireMs(labut2::GpuTimestampMilliseconds(0xaaaa0000000aULL, 0xbbbb00000014ULL, 32, 1000), 0.01,
        "Bits outside the queue's timestamp width must be ignored.");
    RequireMs(labut2::GpuTimestampMilliseconds(100, 100, 64, 1), 0.0,
        "A valid zero duration is distinguishable from unavailable results.");
    std::cout << "[PASS] Valid-bit rollover and timestamp conversion.\n";

    for (const auto bits : {0u, 65u})
        Require(!labut2::GpuTimestampMilliseconds(0, 1, bits, 1), "Invalid valid-bit widths must be rejected.");
    for (const auto period : {0.0, -1.0, std::numeric_limits<double>::infinity(),
            std::numeric_limits<double>::quiet_NaN()})
        Require(!labut2::GpuTimestampMilliseconds(0, 1, 32, period), "Invalid timestamp periods must be rejected.");
    Require(!labut2::GpuTimestampMilliseconds(0, max, 64, std::numeric_limits<double>::max()),
        "Overflowed durations must not enter statistics.");
    std::cout << "[PASS] Invalid and non-finite timestamp rejection.\n";

    labut2::VulkanContext context;
    labut2::GpuFrameTimer timer;
    Require(!timer.Supported() && !timer.ConsumeCompleted(0), "A fresh timer must be unavailable.");
    Require(!timer.Initialize(context, 0), "Zero in-flight slots must not allocate a pool.");
    Require(!timer.Initialize(context, std::numeric_limits<std::uint32_t>::max()),
        "A query count that overflows uint32 must be rejected.");
    api.familyCount = 0;
    Require(!timer.Initialize(context, 2), "Missing queue family must be unsupported.");
    api.familyCount = 1;
    context.graphicsFamilyIndex = 1;
    Require(!timer.Initialize(context, 2), "An out-of-range queue index must be unsupported.");
    context.graphicsFamilyIndex = 0;
    api.validBits = 0;
    Require(!timer.Initialize(context, 2), "A queue without timestamps must be unsupported.");
    api.validBits = 32;
    api.period = 0;
    Require(!timer.Initialize(context, 2), "An invalid physical-device timestamp period must be unsupported.");
    Require(api.creates == 0, "Unsupported devices must not allocate query resources.");
    timer.Begin(VK_NULL_HANDLE, 0);
    timer.End(VK_NULL_HANDLE, 0);
    timer.Submitted(0);
    Require(!timer.ConsumeCompleted(0) && api.reads == 0 && api.writes.empty(),
        "Unsupported timer operations must remain no-ops.");
    std::cout << "[PASS] Unsupported queues and initialization inputs.\n";

    api = {};
    api.createResult = VK_ERROR_OUT_OF_DEVICE_MEMORY;
    Require(!timer.Initialize(context, 2) && !timer.Supported(), "Pool allocation failure must disable profiling.");
    timer.Shutdown();
    Require(api.destroys == 0, "An unsuccessful pool creation must not destroy an unspecified handle.");
    api.createResult = VK_SUCCESS;
    Require(timer.Initialize(context, 3) && timer.Supported() && api.queryCount == 6,
        "Successful initialization must allocate exactly two queries per slot.");
    std::cout << "[PASS] Pool failure and successful allocation.\n";

    timer.Begin(VK_NULL_HANDLE, 2);
    timer.End(VK_NULL_HANDLE, 2);
    Require(api.resets == 1 && api.resetFirst == 4 && api.writes.size() == 2 &&
        api.writes[0] == std::make_pair(VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 4u) &&
        api.writes[1] == std::make_pair(VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 5u),
        "Begin and End must bracket the command buffer using the chosen slot only.");
    timer.Begin(VK_NULL_HANDLE, 3);
    timer.End(VK_NULL_HANDLE, 3);
    timer.Submitted(3);
    Require(!timer.ConsumeCompleted(3) && api.resets == 1 && api.writes.size() == 2,
        "Invalid slots must not record or read queries.");
    Require(!timer.ConsumeCompleted(2) && api.reads == 0,
        "Recording without submission must not publish a measurement.");
    std::cout << "[PASS] Command boundaries and slot isolation.\n";

    timer.Submitted(2);
    RequireMs(timer.ConsumeCompleted(2), 1.0, "Submitted and completed queries must convert using timestampPeriod.");
    Require(api.reads == 1 && api.readFirst == 4, "Only the completed slot must be read.");
    Require(!timer.ConsumeCompleted(2) && api.reads == 1, "Each submission's sample must be consumed only once.");
    std::cout << "[PASS] Submission gating, nonblocking reads and read-once delivery.\n";

    timer.Submitted(0);
    api.beginAvailable = 0;
    Require(!timer.ConsumeCompleted(0), "Unavailable begin timestamp must produce no sample, not zero.");
    api.beginAvailable = 1;
    api.endAvailable = 0;
    timer.Submitted(0);
    Require(!timer.ConsumeCompleted(0), "Unavailable end timestamp must produce no sample.");
    api.endAvailable = 1;
    for (const auto result : {VK_NOT_READY, VK_ERROR_DEVICE_LOST}) {
        api.readResult = result;
        timer.Submitted(0);
        Require(!timer.ConsumeCompleted(0), "Unsuccessful Vulkan query reads must produce no sample.");
    }
    api.readResult = VK_SUCCESS;
    Require(!timer.ConsumeCompleted(0), "A failed read must not leak a stale result into a later frame.");
    std::cout << "[PASS] Unavailable and failed reads never report zero/stale durations.\n";

    timer.Submitted(0);
    timer.Submitted(1);
    const auto readsBeforeDiscard = api.reads;
    timer.DiscardPending();
    Require(!timer.ConsumeCompleted(0) && !timer.ConsumeCompleted(1) && api.reads == readsBeforeDiscard,
        "Recreation discard must drop every pending slot without calling Vulkan.");
    timer.Submitted(1);
    RequireMs(timer.ConsumeCompleted(1), 1.0, "A new submission after discard must still be measurable.");
    std::cout << "[PASS] Pending samples can be discarded and resumed.\n";

    const auto destroysBefore = api.destroys;
    Require(timer.Initialize(context, 1), "Timer reinitialization must succeed.");
    Require(api.destroys == destroysBefore + 1 && api.queryCount == 2,
        "Reinitialization must first release the previous pool.");
    timer.Submitted(0);
    timer.Shutdown();
    Require(!timer.Supported() && !timer.ConsumeCompleted(0) && api.destroys == destroysBefore + 2,
        "Shutdown must release its pool and pending state.");
    timer.Shutdown();
    Require(api.destroys == destroysBefore + 2, "Repeated shutdown must not destroy a pool twice.");
    {
        labut2::GpuFrameTimer owned;
        Require(owned.Initialize(context, 1), "Scoped timer initialization must succeed.");
    }
    Require(api.destroys == destroysBefore + 3, "Timer destruction must release its pool once.");
    std::cout << "[PASS] Reinitialize, explicit shutdown and destructor ownership.\n9 GPU timer CPU regression groups passed.\n";
    return 0;
}
catch (const std::exception& error) {
    std::cerr << "[FAIL] " << error.what() << '\n';
    return 1;
}
