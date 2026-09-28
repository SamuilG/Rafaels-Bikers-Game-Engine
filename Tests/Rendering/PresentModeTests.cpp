#include "Runtime/Rhi/PresentMode.hpp"
#include <iostream>
#include <stdexcept>

namespace {
void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
}

int main() try {
    const std::unordered_set<VkPresentModeKHR> allModes{
        VK_PRESENT_MODE_IMMEDIATE_KHR, VK_PRESENT_MODE_MAILBOX_KHR,
        VK_PRESENT_MODE_FIFO_KHR, VK_PRESENT_MODE_FIFO_RELAXED_KHR};
    Require(labut2::ChoosePresentMode(true, allModes) == VK_PRESENT_MODE_FIFO_KHR,
        "VSync must use strict FIFO even when relaxed FIFO and immediate are available.");
    std::cout << "[PASS] VSync remains tear-free.\n";

    Require(labut2::ChoosePresentMode(false, allModes) == VK_PRESENT_MODE_IMMEDIATE_KHR,
        "Unlimited must prefer immediate over mailbox when both are supported.");
    std::cout << "[PASS] Unlimited prefers immediate.\n";

    const std::unordered_set<VkPresentModeKHR> mailboxOnly{
        VK_PRESENT_MODE_FIFO_KHR, VK_PRESENT_MODE_MAILBOX_KHR};
    Require(labut2::ChoosePresentMode(false, mailboxOnly) == VK_PRESENT_MODE_MAILBOX_KHR,
        "Mailbox must be used when immediate presentation is unsupported.");
    Require(labut2::ChoosePresentMode(true, mailboxOnly) == VK_PRESENT_MODE_FIFO_KHR,
        "Switching VSync back on must return to FIFO.");
    std::cout << "[PASS] Mailbox fallback and VSync restoration.\n";

    const std::unordered_set<VkPresentModeKHR> fifoOnly{VK_PRESENT_MODE_FIFO_KHR};
    Require(labut2::ChoosePresentMode(false, fifoOnly) == VK_PRESENT_MODE_FIFO_KHR,
        "An unsupported unlimited request must expose FIFO as its actual mode.");
    Require(labut2::ChoosePresentMode(false, {VK_PRESENT_MODE_FIFO_RELAXED_KHR,
        VK_PRESENT_MODE_FIFO_KHR}) == VK_PRESENT_MODE_FIFO_KHR,
        "Relaxed FIFO must not masquerade as unlimited presentation.");
    std::cout << "[PASS] FIFO fallback for constrained surfaces.\n";

    Require(labut2::ChoosePresentMode(true, {}) == VK_PRESENT_MODE_FIFO_KHR,
        "An empty capability set must have a deterministic synchronized default.");
    Require(labut2::ChoosePresentMode(false, {}) == VK_PRESENT_MODE_FIFO_KHR,
        "An empty capability set must not advertise unsupported unlimited presentation.");
    std::cout << "[PASS] Empty capability set fallback.\n5 present mode regression groups passed.\n";
    return 0;
}
catch (const std::exception& error) {
    std::cerr << "[FAIL] " << error.what() << '\n';
    return 1;
}
