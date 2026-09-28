#pragma once

#include "vulkan_context.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <vector>

namespace labut2 {

inline std::optional<double> GpuTimestampMilliseconds(std::uint64_t begin, std::uint64_t end,
    std::uint32_t validBits, double periodNanoseconds) {
    if (validBits == 0 || validBits > 64 || !std::isfinite(periodNanoseconds) || periodNanoseconds <= 0.0)
        return std::nullopt;
    const auto mask = validBits == 64 ? std::numeric_limits<std::uint64_t>::max()
        : (std::uint64_t{1} << validBits) - 1;
    // Vulkan timestamps wrap at the queue family's valid bit width.
    const double ms = static_cast<double>((end - begin) & mask) * periodNanoseconds / 1000000.0;
    return std::isfinite(ms) ? std::optional<double>(ms) : std::nullopt;
}

// Two queries per in-flight command buffer. Read only after that slot's existing
// fence has completed; profiling never introduces a GPU wait of its own.
class GpuFrameTimer {
public:
    GpuFrameTimer() = default;
    ~GpuFrameTimer() { Shutdown(); }
    GpuFrameTimer(const GpuFrameTimer&) = delete;
    GpuFrameTimer& operator=(const GpuFrameTimer&) = delete;

    bool Initialize(const VulkanContext& context, std::size_t slots) {
        Shutdown();
        if (!slots || slots > std::numeric_limits<std::uint32_t>::max() / 2) return false;
        std::uint32_t count = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(context.physicalDevice, &count, nullptr);
        std::vector<VkQueueFamilyProperties> families(count);
        vkGetPhysicalDeviceQueueFamilyProperties(context.physicalDevice, &count, families.data());
        if (context.graphicsFamilyIndex >= count) return false;
        mValidBits = families[context.graphicsFamilyIndex].timestampValidBits;
        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties(context.physicalDevice, &properties);
        mPeriod = properties.limits.timestampPeriod;
        if (!GpuTimestampMilliseconds(0, 1, mValidBits, mPeriod)) return false;
        VkQueryPoolCreateInfo info{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
        info.queryType = VK_QUERY_TYPE_TIMESTAMP;
        info.queryCount = static_cast<std::uint32_t>(slots * 2);
        if (vkCreateQueryPool(context.device, &info, nullptr, &mPool) != VK_SUCCESS) {
            mPool = VK_NULL_HANDLE;
            return false;
        }
        mDevice = context.device;
        mPending.assign(slots, false);
        return true;
    }

    // Caller completes outstanding device work before destroying the pool.
    void Shutdown() {
        if (mPool) vkDestroyQueryPool(mDevice, mPool, nullptr);
        mPool = VK_NULL_HANDLE;
        mDevice = VK_NULL_HANDLE;
        mPending.clear();
    }
    bool Supported() const { return mPool != VK_NULL_HANDLE; }
    void DiscardPending() { std::fill(mPending.begin(), mPending.end(), false); }

    void Begin(VkCommandBuffer command, std::uint32_t slot) const {
        if (!Supported() || slot >= mPending.size()) return;
        vkCmdResetQueryPool(command, mPool, slot * 2, 2);
        vkCmdWriteTimestamp(command, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, mPool, slot * 2);
    }
    void End(VkCommandBuffer command, std::uint32_t slot) const {
        if (!Supported() || slot >= mPending.size()) return;
        vkCmdWriteTimestamp(command, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, mPool, slot * 2 + 1);
    }
    void Submitted(std::uint32_t slot) {
        if (Supported() && slot < mPending.size()) mPending[slot] = true;
    }
    std::optional<double> ConsumeCompleted(std::uint32_t slot) {
        if (!Supported() || slot >= mPending.size() || !mPending[slot]) return std::nullopt;
        mPending[slot] = false;
        struct Result { std::uint64_t value, available; } values[2]{};
        const auto result = vkGetQueryPoolResults(mDevice, mPool, slot * 2, 2,
            sizeof(values), values, sizeof(Result),
            VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WITH_AVAILABILITY_BIT);
        if (result != VK_SUCCESS || !values[0].available || !values[1].available) return std::nullopt;
        return GpuTimestampMilliseconds(values[0].value, values[1].value, mValidBits, mPeriod);
    }

private:
    VkDevice mDevice = VK_NULL_HANDLE;
    VkQueryPool mPool = VK_NULL_HANDLE;
    std::uint32_t mValidBits = 0;
    double mPeriod = 0.0;
    std::vector<bool> mPending;
};

} // namespace labut2
