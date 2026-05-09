#pragma once

#include "../Renderer/RenderUtilities/camera.hpp"
#include "../../../ThirdParty/OpenXR-SDK-main/include/openxr/openxr.h"
#include <array>
#include <string_view>
#include <string>
#include <vector>
#include <volk/volk.h>

struct GLFWwindow;

namespace engine {
    class InputSystem;

    struct XrFrameState
    {
        bool shouldRender = false;
        bool hasValidViews = false;
        StereoCameraFrame stereoFrame{};
    };

    struct XrVulkanRequirements
    {
        std::uint32_t apiVersion = VK_MAKE_API_VERSION(0, 1, 0, 0);
        std::vector<std::string> instanceExtensions;
        std::vector<std::string> deviceExtensions;
    };

    struct XrSwapchainEyeImage
    {
        VkImage image = VK_NULL_HANDLE;
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        VkFormat format = VK_FORMAT_UNDEFINED;
    };

    class IXrSystem
    {
    public:
        virtual ~IXrSystem() = default;

        virtual bool Initialize(GLFWwindow* aWindow) = 0;
        virtual bool InitializeSession(
            VkInstance aInstance,
            VkPhysicalDevice aPhysicalDevice,
            VkDevice aDevice,
            std::uint32_t aGraphicsFamilyIndex,
            std::uint32_t aQueueIndex) = 0;
        virtual void Shutdown() = 0;

        virtual void PollEvents() = 0;
        virtual bool BeginFrame() = 0;
        virtual void EndFrame() = 0;

        virtual bool TryGetFrameState(XrFrameState& aOutFrameState) const = 0;
        virtual bool TryGetVulkanRequirements(XrVulkanRequirements& aOutRequirements) const = 0;
        virtual bool TryGetSwapchainImages(std::array<XrSwapchainEyeImage, 2>& aOutSwapchainImages) const = 0;
        virtual void ApplyInputState(InputSystem& aInputSystem) = 0;
        virtual void MarkSwapchainImagesRendered() = 0;

        virtual void SetEnabled(bool aEnabled) = 0;
        virtual bool IsEnabled() const = 0;
        virtual bool IsAvailable() const = 0;
        virtual bool IsSessionRunning() const = 0;
        virtual bool IsSessionCreated() const = 0;
        virtual const char* GetRuntimeName() const = 0;
        virtual std::string_view GetStatusText() const = 0;
    };

} // namespace engine
