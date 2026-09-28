#pragma once

#include <vulkan/vulkan_core.h>
#include <unordered_set>

namespace labut2 {

// FIFO is required by Vulkan WSI and gives a tear-free synchronized default.
// Unlimited prefers immediate presentation; mailbox is the tear-free fallback
// when immediate is unavailable. An empty/limited set safely falls back to FIFO.
inline VkPresentModeKHR ChoosePresentMode(
    bool vsyncEnabled, const std::unordered_set<VkPresentModeKHR>& supportedModes) {
    if (!vsyncEnabled) {
        if (supportedModes.count(VK_PRESENT_MODE_IMMEDIATE_KHR))
            return VK_PRESENT_MODE_IMMEDIATE_KHR;
        if (supportedModes.count(VK_PRESENT_MODE_MAILBOX_KHR))
            return VK_PRESENT_MODE_MAILBOX_KHR;
    }
    return VK_PRESENT_MODE_FIFO_KHR;
}

}
