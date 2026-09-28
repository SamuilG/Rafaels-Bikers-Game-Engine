#pragma once

#include <vulkan/vulkan_core.h>
#include <algorithm>
#include <cstdint>
#include <limits>
#include <optional>

namespace labut2 {

// A minimized/unavailable surface has no drawable extent. Do not clamp a
// zero framebuffer to the minimum: defer recreation until a later frame.
inline std::optional<VkExtent2D> ChooseSwapchainExtent(
    const VkSurfaceCapabilitiesKHR& caps, int framebufferWidth, int framebufferHeight) {
    if (framebufferWidth <= 0 || framebufferHeight <= 0 ||
        caps.maxImageExtent.width == 0 || caps.maxImageExtent.height == 0 ||
        caps.minImageExtent.width > caps.maxImageExtent.width ||
        caps.minImageExtent.height > caps.maxImageExtent.height) return std::nullopt;

    constexpr auto undefined = std::numeric_limits<std::uint32_t>::max();
    const bool undefinedWidth = caps.currentExtent.width == undefined;
    const bool undefinedHeight = caps.currentExtent.height == undefined;
    if (undefinedWidth != undefinedHeight) return std::nullopt;

    VkExtent2D extent = caps.currentExtent;
    if (undefinedWidth) {
        extent.width = std::clamp(static_cast<std::uint32_t>(framebufferWidth),
            caps.minImageExtent.width, caps.maxImageExtent.width);
        extent.height = std::clamp(static_cast<std::uint32_t>(framebufferHeight),
            caps.minImageExtent.height, caps.maxImageExtent.height);
    }
    if (extent.width == 0 || extent.height == 0 ||
        extent.width < caps.minImageExtent.width || extent.height < caps.minImageExtent.height ||
        extent.width > caps.maxImageExtent.width || extent.height > caps.maxImageExtent.height)
        return std::nullopt;
    return extent;
}

}
