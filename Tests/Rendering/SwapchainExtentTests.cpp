#include "Runtime/Rhi/SwapchainExtent.hpp"
#include <iostream>
#include <stdexcept>

namespace {
void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

VkSurfaceCapabilitiesKHR Capabilities() {
    VkSurfaceCapabilitiesKHR caps{};
    caps.currentExtent = {1280, 720};
    caps.minImageExtent = {1, 1};
    caps.maxImageExtent = {8192, 8192};
    return caps;
}
}

int main() try {
    auto caps = Capabilities();
    auto extent = labut2::ChooseSwapchainExtent(caps, 1600, 900);
    Require(extent && extent->width == 1280 && extent->height == 720,
        "A fixed surface extent must take precedence over the framebuffer size.");
    std::cout << "[PASS] Fixed surface size.\n";

    caps.currentExtent = {0, 0};
    Require(!labut2::ChooseSwapchainExtent(caps, 1280, 720), "Zero Win32 surface must defer.");
    caps.currentExtent = {1280, 0};
    Require(!labut2::ChooseSwapchainExtent(caps, 1280, 720), "Zero height must defer.");
    std::cout << "[PASS] Unavailable fixed surface.\n";

    caps = Capabilities();
    Require(!labut2::ChooseSwapchainExtent(caps, 0, 0), "A minimized framebuffer must defer even with stale valid capabilities.");
    Require(!labut2::ChooseSwapchainExtent(caps, -1, 720), "Negative framebuffer width must not wrap to unsigned.");
    std::cout << "[PASS] Minimized or invalid framebuffer.\n";

    constexpr auto undefined = std::numeric_limits<std::uint32_t>::max();
    caps.currentExtent = {undefined, undefined};
    extent = labut2::ChooseSwapchainExtent(caps, 1920, 1080);
    Require(extent && extent->width == 1920 && extent->height == 1080, "Undefined surface uses framebuffer dimensions.");
    std::cout << "[PASS] Variable surface size.\n";

    caps.minImageExtent = {64, 64};
    caps.maxImageExtent = {4096, 4096};
    extent = labut2::ChooseSwapchainExtent(caps, 32, 9000);
    Require(extent && extent->width == 64 && extent->height == 4096, "Drawable framebuffer is constrained by legal limits.");
    Require(!labut2::ChooseSwapchainExtent(caps, 0, 9000), "Minimized width must not be clamped to 64.");
    std::cout << "[PASS] Clamp drawable dimensions, never minimized ones.\n";

    caps = Capabilities();
    caps.maxImageExtent = {0, 0};
    Require(!labut2::ChooseSwapchainExtent(caps, 1280, 720), "Zero capability limit must defer.");
    caps = Capabilities();
    caps.minImageExtent = {9000, 9000};
    Require(!labut2::ChooseSwapchainExtent(caps, 1280, 720), "Inverted range must not reach std::clamp.");
    std::cout << "[PASS] Unavailable capability ranges.\n";

    caps = Capabilities();
    caps.currentExtent = {undefined, 720};
    Require(!labut2::ChooseSwapchainExtent(caps, 1280, 720), "Partial undefined dimensions must defer.");
    caps.currentExtent = {9000, 720};
    Require(!labut2::ChooseSwapchainExtent(caps, 1280, 720), "An invalid fixed extent must defer.");
    std::cout << "[PASS] Inconsistent capability dimensions.\n";

    caps = Capabilities();
    Require(!labut2::ChooseSwapchainExtent(caps, 0, 0), "Minimize defers.");
    extent = labut2::ChooseSwapchainExtent(caps, 1280, 720);
    Require(extent && extent->width == 1280 && extent->height == 720, "Restore must immediately make recreation eligible again.");
    std::cout << "[PASS] Restore after defer.\n8 swapchain extent regression groups passed.\n";
    return 0;
}
catch (const std::exception& error) {
    std::cerr << "[FAIL] " << error.what() << '\n';
    return 1;
}
