#pragma once

#include "IXrSystem.hpp"

#if !defined(XR_USE_GRAPHICS_API_VULKAN)
#define XR_USE_GRAPHICS_API_VULKAN
#endif
#include "../../../ThirdParty/OpenXR-SDK-main/include/openxr/openxr_platform.h"
#include <array>
#include <string>
#include <vector>

namespace engine {

    class OpenXrSystem final : public IXrSystem
    {
    public:
        OpenXrSystem() = default;
        ~OpenXrSystem() override = default;

        bool Initialize(GLFWwindow* aWindow) override;
        bool InitializeSession(
            VkInstance aInstance,
            VkPhysicalDevice aPhysicalDevice,
            VkDevice aDevice,
            std::uint32_t aGraphicsFamilyIndex,
            std::uint32_t aQueueIndex) override;
        void Shutdown() override;

        void PollEvents() override;
        bool BeginFrame() override;
        void EndFrame() override;

        bool TryGetFrameState(XrFrameState& aOutFrameState) const override;
        bool TryGetVulkanRequirements(XrVulkanRequirements& aOutRequirements) const override;
        bool TryGetSwapchainImages(std::array<XrSwapchainEyeImage, 2>& aOutSwapchainImages) const override;
        void ApplyInputState(InputSystem& aInputSystem) override;
        void MarkSwapchainImagesRendered() override;
        bool CreateVulkanInstanceWithXr(VkInstanceCreateInfo const& aCreateInfo, VkInstance& aOutInstance, VkResult& aOutVkResult);
        bool GetVulkanGraphicsDeviceWithXr(VkInstance aInstance, VkPhysicalDevice& aOutPhysicalDevice);
        bool CreateVulkanDeviceWithXr(VkPhysicalDevice aPhysicalDevice, VkDeviceCreateInfo const& aCreateInfo, VkDevice& aOutDevice, VkResult& aOutVkResult);

        void SetEnabled(bool aEnabled) override;
        bool IsEnabled() const override;
        bool IsAvailable() const override;
        bool IsSessionRunning() const override;
        bool IsSessionCreated() const override;
        const char* GetRuntimeName() const override;
        std::string_view GetStatusText() const override;

    private:
        struct EyeSwapchain
        {
            XrSwapchain handle = XR_NULL_HANDLE;
            VkFormat format = VK_FORMAT_UNDEFINED;
            std::uint32_t width = 0;
            std::uint32_t height = 0;
            std::vector<XrSwapchainImageVulkanKHR> images;
            std::uint32_t acquiredIndex = 0;
            bool acquired = false;
        };

        bool LoadLoaderFunctions();
        void ReleaseLoader();
        void RefreshStatus();
        bool CreateSwapchains();
        void DestroySwapchains();

        GLFWwindow* mWindow = nullptr;
        bool mEnabled = false;
        bool mInitialized = false;
        bool mSessionRunning = false;
        bool mLoaderAvailable = false;
        bool mSystemAvailable = false;
        XrFrameState mFrameState{};
        void* mLoaderModule = nullptr;
        XrInstance mInstance = XR_NULL_HANDLE;
        XrSystemId mSystemId = XR_NULL_SYSTEM_ID;
        PFN_xrGetInstanceProcAddr mGetInstanceProcAddr = nullptr;
        PFN_xrEnumerateInstanceExtensionProperties mEnumerateInstanceExtensionProperties = nullptr;
        PFN_xrCreateInstance mCreateInstance = nullptr;
        PFN_xrDestroyInstance mDestroyInstance = nullptr;
        PFN_xrGetInstanceProperties mGetInstanceProperties = nullptr;
        PFN_xrGetSystem mGetSystem = nullptr;
        PFN_xrCreateSession mCreateSession = nullptr;
        PFN_xrDestroySession mDestroySession = nullptr;
        PFN_xrCreateReferenceSpace mCreateReferenceSpace = nullptr;
        PFN_xrDestroySpace mDestroySpace = nullptr;
        PFN_xrStringToPath mStringToPath = nullptr;
        PFN_xrCreateActionSet mCreateActionSet = nullptr;
        PFN_xrDestroyActionSet mDestroyActionSet = nullptr;
        PFN_xrCreateAction mCreateAction = nullptr;
        PFN_xrDestroyAction mDestroyAction = nullptr;
        PFN_xrSuggestInteractionProfileBindings mSuggestInteractionProfileBindings = nullptr;
        PFN_xrAttachSessionActionSets mAttachSessionActionSets = nullptr;
        PFN_xrSyncActions mSyncActions = nullptr;
        PFN_xrGetActionStateFloat mGetActionStateFloat = nullptr;
        PFN_xrGetActionStateVector2f mGetActionStateVector2f = nullptr;
        PFN_xrPollEvent mPollEvent = nullptr;
        PFN_xrBeginSession mBeginSession = nullptr;
        PFN_xrEndSession mEndSession = nullptr;
        PFN_xrWaitFrame mWaitFrame = nullptr;
        PFN_xrBeginFrame mBeginFrameFn = nullptr;
        PFN_xrEndFrame mEndFrameFn = nullptr;
        PFN_xrLocateViews mLocateViews = nullptr;
        PFN_xrEnumerateViewConfigurationViews mEnumerateViewConfigurationViews = nullptr;
        PFN_xrEnumerateSwapchainFormats mEnumerateSwapchainFormats = nullptr;
        PFN_xrCreateSwapchain mCreateSwapchainFn = nullptr;
        PFN_xrDestroySwapchain mDestroySwapchainFn = nullptr;
        PFN_xrEnumerateSwapchainImages mEnumerateSwapchainImages = nullptr;
        PFN_xrAcquireSwapchainImage mAcquireSwapchainImage = nullptr;
        PFN_xrWaitSwapchainImage mWaitSwapchainImage = nullptr;
        PFN_xrReleaseSwapchainImage mReleaseSwapchainImage = nullptr;
        PFN_xrCreateVulkanInstanceKHR mCreateVulkanInstanceKHR = nullptr;
        PFN_xrCreateVulkanDeviceKHR mCreateVulkanDeviceKHR = nullptr;
        PFN_xrGetVulkanGraphicsDevice2KHR mGetVulkanGraphicsDevice2KHR = nullptr;
        PFN_xrGetVulkanGraphicsRequirements2KHR mGetVulkanGraphicsRequirements2KHR = nullptr;
        PFN_xrGetVulkanGraphicsRequirementsKHR mGetVulkanGraphicsRequirementsKHR = nullptr;
        PFN_xrGetVulkanInstanceExtensionsKHR mGetVulkanInstanceExtensionsKHR = nullptr;
        PFN_xrGetVulkanDeviceExtensionsKHR mGetVulkanDeviceExtensionsKHR = nullptr;
        std::string mRuntimeName = "OpenXR";
        std::string mStatusText = "OpenXR not initialized";
        std::string mActiveRuntimePath;
        XrResult mLastCreateInstanceResult = XR_SUCCESS;
        XrResult mLastGetSystemResult = XR_SUCCESS;
        XrSession mSession = XR_NULL_HANDLE;
        XrSpace mAppSpace = XR_NULL_HANDLE;
        XrSessionState mSessionState = XR_SESSION_STATE_UNKNOWN;
        bool mSessionBegun = false;
        bool mFrameBegun = false;
        bool mSwapchainImagesRendered = false;
        XrTime mPredictedDisplayTime = 0;
        std::vector<XrViewConfigurationView> mViewConfigs;
        std::vector<XrView> mLocatedViews;
        std::array<EyeSwapchain, 2> mEyeSwapchains{};
        std::array<XrSwapchainEyeImage, 2> mActiveSwapchainImages{};
        std::array<XrCompositionLayerProjectionView, 2> mProjectionViews{};
        XrCompositionLayerProjection mProjectionLayer{ XR_TYPE_COMPOSITION_LAYER_PROJECTION };
        std::array<XrCompositionLayerBaseHeader const*, 1> mProjectionLayers{};
        XrActionSet mInputActionSet = XR_NULL_HANDLE;
        XrAction mMoveForwardAction = XR_NULL_HANDLE;
        XrAction mMoveBackwardAction = XR_NULL_HANDLE;
        XrAction mSteerAction = XR_NULL_HANDLE;
        XrPath mLeftHandPath = XR_NULL_PATH;
        XrPath mRightHandPath = XR_NULL_PATH;
        bool mVulkanEnableAvailable = false;
        bool mVulkanEnable2Available = false;
        XrVulkanRequirements mVulkanRequirements{};
    };

} // namespace engine
