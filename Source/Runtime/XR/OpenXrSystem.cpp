#include "OpenXrSystem.hpp"

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <sstream>
#include <vector>

#include "../Input/InputSystem.hpp"

namespace
{
    std::uint32_t ToVkApiVersion(XrVersion aVersion)
    {
        return VK_MAKE_API_VERSION(
            0,
            XR_VERSION_MAJOR(aVersion),
            XR_VERSION_MINOR(aVersion),
            XR_VERSION_PATCH(aVersion)
        );
    }

    std::wstring ReadActiveRuntimeRegistryPath()
    {
        wchar_t buffer[1024]{};
        DWORD bufferSize = sizeof(buffer);
        LONG const result = RegGetValueW(
            HKEY_LOCAL_MACHINE,
            L"SOFTWARE\\Khronos\\OpenXR\\1",
            L"ActiveRuntime",
            RRF_RT_REG_SZ,
            nullptr,
            buffer,
            &bufferSize
        );

        if (ERROR_SUCCESS != result || buffer[0] == L'\0') {
            return {};
        }

        return std::wstring(buffer);
    }

    glm::mat4 PoseToWorldMatrix(XrPosef const& pose)
    {
        glm::quat orientation(pose.orientation.w, pose.orientation.x, pose.orientation.y, pose.orientation.z);
        glm::mat4 rotation = glm::mat4_cast(orientation);
        glm::mat4 translation = glm::translate(glm::mat4(1.0f), glm::vec3(
            pose.position.x,
            pose.position.y,
            pose.position.z));
        return translation * rotation;
    }

    glm::mat4 ProjectionFromFov(XrFovf const& fov, float nearPlane, float farPlane)
    {
        float const tanLeft = std::tan(fov.angleLeft);
        float const tanRight = std::tan(fov.angleRight);
        float const tanDown = std::tan(fov.angleDown);
        float const tanUp = std::tan(fov.angleUp);

        float const width = tanRight - tanLeft;
        float const height = tanDown - tanUp;

        glm::mat4 projection(0.0f);
        projection[0][0] = 2.0f / width;
        projection[1][1] = 2.0f / height;
        projection[2][0] = (tanRight + tanLeft) / width;
        projection[2][1] = (tanDown + tanUp) / height;
        projection[2][2] = -farPlane / (farPlane - nearPlane);
        projection[2][3] = -1.0f;
        projection[3][2] = -(farPlane * nearPlane) / (farPlane - nearPlane);
        return projection;
    }

}

namespace engine {

    bool OpenXrSystem::Initialize(GLFWwindow* aWindow)
    {
        mWindow = aWindow;
        mInitialized = true;
        mSessionRunning = false;
        mFrameState = {};
        mVulkanRequirements = {};

        std::wstring const activeRuntimePath = ReadActiveRuntimeRegistryPath();
        if (!activeRuntimePath.empty()) {
            int const requiredSize = WideCharToMultiByte(CP_UTF8, 0, activeRuntimePath.c_str(), -1, nullptr, 0, nullptr, nullptr);
            if (requiredSize > 1) {
                mActiveRuntimePath.resize(static_cast<size_t>(requiredSize - 1));
                WideCharToMultiByte(CP_UTF8, 0, activeRuntimePath.c_str(), -1, mActiveRuntimePath.data(), requiredSize, nullptr, nullptr);
            }
        }

        if (!LoadLoaderFunctions()) {
            RefreshStatus();
            return true;
        }

        const char* enabledExtensions[2]{};
        uint32_t enabledExtensionCount = 0;
        if (mEnumerateInstanceExtensionProperties) {
            uint32_t extensionCount = 0;
            if (XR_SUCCEEDED(mEnumerateInstanceExtensionProperties(nullptr, 0, &extensionCount, nullptr)) && extensionCount > 0) {
                std::vector<XrExtensionProperties> extensions(extensionCount, { XR_TYPE_EXTENSION_PROPERTIES });
                if (XR_SUCCEEDED(mEnumerateInstanceExtensionProperties(nullptr, extensionCount, &extensionCount, extensions.data()))) {
                    for (auto const& extension : extensions) {
                        if (0 == std::strcmp(extension.extensionName, XR_KHR_VULKAN_ENABLE2_EXTENSION_NAME)) {
                            mVulkanEnable2Available = true;
                            enabledExtensions[enabledExtensionCount++] = XR_KHR_VULKAN_ENABLE2_EXTENSION_NAME;
                            continue;
                        }
                        if (0 == std::strcmp(extension.extensionName, XR_KHR_VULKAN_ENABLE_EXTENSION_NAME)) {
                            mVulkanEnableAvailable = true;
                            enabledExtensions[enabledExtensionCount++] = XR_KHR_VULKAN_ENABLE_EXTENSION_NAME;
                        }
                    }
                }
            }
        }

        XrInstanceCreateInfo createInfo{ XR_TYPE_INSTANCE_CREATE_INFO };
        std::strncpy(createInfo.applicationInfo.applicationName, "RafaelsBikersEngine", XR_MAX_APPLICATION_NAME_SIZE - 1);
        std::strncpy(createInfo.applicationInfo.engineName, "RafaelsBikersEngine", XR_MAX_ENGINE_NAME_SIZE - 1);
        createInfo.applicationInfo.applicationVersion = 1;
        createInfo.applicationInfo.engineVersion = 1;
        // Prefer a conservative API target for broad runtime compatibility.
        // SteamVR runtimes are commonly 1.0-capable even when newer headers are present.
        createInfo.applicationInfo.apiVersion = XR_API_VERSION_1_0;
        createInfo.enabledExtensionCount = enabledExtensionCount;
        createInfo.enabledExtensionNames = enabledExtensions;

        XrResult const instanceResult = mCreateInstance(&createInfo, &mInstance);
        mLastCreateInstanceResult = instanceResult;
        if (XR_FAILED(instanceResult)) {
            mStatusText = "OpenXR loader detected, but xrCreateInstance failed with code " + std::to_string(instanceResult) + ".";
            ReleaseLoader();
            return true;
        }

        PFN_xrVoidFunction fn = nullptr;
        if (XR_FAILED(mGetInstanceProcAddr(mInstance, "xrDestroyInstance", &fn))) {
            mStatusText = "OpenXR instance created, but xrDestroyInstance could not be resolved.";
            mInstance = XR_NULL_HANDLE;
            ReleaseLoader();
            return true;
        }
        mDestroyInstance = reinterpret_cast<PFN_xrDestroyInstance>(fn);

        fn = nullptr;
        if (XR_FAILED(mGetInstanceProcAddr(mInstance, "xrGetInstanceProperties", &fn))) {
            mStatusText = "OpenXR instance created, but xrGetInstanceProperties could not be resolved.";
            mDestroyInstance(mInstance);
            mInstance = XR_NULL_HANDLE;
            ReleaseLoader();
            return true;
        }
        mGetInstanceProperties = reinterpret_cast<PFN_xrGetInstanceProperties>(fn);

        fn = nullptr;
        if (XR_FAILED(mGetInstanceProcAddr(mInstance, "xrGetSystem", &fn))) {
            mStatusText = "OpenXR instance created, but xrGetSystem could not be resolved.";
            mDestroyInstance(mInstance);
            mInstance = XR_NULL_HANDLE;
            ReleaseLoader();
            return true;
        }
        mGetSystem = reinterpret_cast<PFN_xrGetSystem>(fn);

        fn = nullptr;
        if (XR_SUCCEEDED(mGetInstanceProcAddr(mInstance, "xrCreateSession", &fn))) {
            mCreateSession = reinterpret_cast<PFN_xrCreateSession>(fn);
        }

        fn = nullptr;
        if (XR_SUCCEEDED(mGetInstanceProcAddr(mInstance, "xrDestroySession", &fn))) {
            mDestroySession = reinterpret_cast<PFN_xrDestroySession>(fn);
        }

        fn = nullptr;
        if (XR_SUCCEEDED(mGetInstanceProcAddr(mInstance, "xrPollEvent", &fn))) {
            mPollEvent = reinterpret_cast<PFN_xrPollEvent>(fn);
        }

        fn = nullptr;
        if (XR_SUCCEEDED(mGetInstanceProcAddr(mInstance, "xrBeginSession", &fn))) {
            mBeginSession = reinterpret_cast<PFN_xrBeginSession>(fn);
        }

        fn = nullptr;
        if (XR_SUCCEEDED(mGetInstanceProcAddr(mInstance, "xrEndSession", &fn))) {
            mEndSession = reinterpret_cast<PFN_xrEndSession>(fn);
        }

        fn = nullptr;
        if (XR_SUCCEEDED(mGetInstanceProcAddr(mInstance, "xrWaitFrame", &fn))) {
            mWaitFrame = reinterpret_cast<PFN_xrWaitFrame>(fn);
        }

        fn = nullptr;
        if (XR_SUCCEEDED(mGetInstanceProcAddr(mInstance, "xrBeginFrame", &fn))) {
            mBeginFrameFn = reinterpret_cast<PFN_xrBeginFrame>(fn);
        }

        fn = nullptr;
        if (XR_SUCCEEDED(mGetInstanceProcAddr(mInstance, "xrEndFrame", &fn))) {
            mEndFrameFn = reinterpret_cast<PFN_xrEndFrame>(fn);
        }

        fn = nullptr;
        if (XR_SUCCEEDED(mGetInstanceProcAddr(mInstance, "xrLocateViews", &fn))) {
            mLocateViews = reinterpret_cast<PFN_xrLocateViews>(fn);
        }

        fn = nullptr;
        if (XR_SUCCEEDED(mGetInstanceProcAddr(mInstance, "xrEnumerateViewConfigurationViews", &fn))) {
            mEnumerateViewConfigurationViews = reinterpret_cast<PFN_xrEnumerateViewConfigurationViews>(fn);
        }

        fn = nullptr;
        if (XR_SUCCEEDED(mGetInstanceProcAddr(mInstance, "xrEnumerateSwapchainFormats", &fn))) {
            mEnumerateSwapchainFormats = reinterpret_cast<PFN_xrEnumerateSwapchainFormats>(fn);
        }

        fn = nullptr;
        if (XR_SUCCEEDED(mGetInstanceProcAddr(mInstance, "xrCreateSwapchain", &fn))) {
            mCreateSwapchainFn = reinterpret_cast<PFN_xrCreateSwapchain>(fn);
        }

        fn = nullptr;
        if (XR_SUCCEEDED(mGetInstanceProcAddr(mInstance, "xrDestroySwapchain", &fn))) {
            mDestroySwapchainFn = reinterpret_cast<PFN_xrDestroySwapchain>(fn);
        }

        fn = nullptr;
        if (XR_SUCCEEDED(mGetInstanceProcAddr(mInstance, "xrEnumerateSwapchainImages", &fn))) {
            mEnumerateSwapchainImages = reinterpret_cast<PFN_xrEnumerateSwapchainImages>(fn);
        }

        fn = nullptr;
        if (XR_SUCCEEDED(mGetInstanceProcAddr(mInstance, "xrAcquireSwapchainImage", &fn))) {
            mAcquireSwapchainImage = reinterpret_cast<PFN_xrAcquireSwapchainImage>(fn);
        }

        fn = nullptr;
        if (XR_SUCCEEDED(mGetInstanceProcAddr(mInstance, "xrWaitSwapchainImage", &fn))) {
            mWaitSwapchainImage = reinterpret_cast<PFN_xrWaitSwapchainImage>(fn);
        }

        fn = nullptr;
        if (XR_SUCCEEDED(mGetInstanceProcAddr(mInstance, "xrReleaseSwapchainImage", &fn))) {
            mReleaseSwapchainImage = reinterpret_cast<PFN_xrReleaseSwapchainImage>(fn);
        }

        fn = nullptr;
        if (XR_SUCCEEDED(mGetInstanceProcAddr(mInstance, "xrCreateReferenceSpace", &fn))) {
            mCreateReferenceSpace = reinterpret_cast<PFN_xrCreateReferenceSpace>(fn);
        }

        fn = nullptr;
        if (XR_SUCCEEDED(mGetInstanceProcAddr(mInstance, "xrStringToPath", &fn))) {
            mStringToPath = reinterpret_cast<PFN_xrStringToPath>(fn);
        }

        fn = nullptr;
        if (XR_SUCCEEDED(mGetInstanceProcAddr(mInstance, "xrCreateActionSet", &fn))) {
            mCreateActionSet = reinterpret_cast<PFN_xrCreateActionSet>(fn);
        }

        fn = nullptr;
        if (XR_SUCCEEDED(mGetInstanceProcAddr(mInstance, "xrDestroyActionSet", &fn))) {
            mDestroyActionSet = reinterpret_cast<PFN_xrDestroyActionSet>(fn);
        }

        fn = nullptr;
        if (XR_SUCCEEDED(mGetInstanceProcAddr(mInstance, "xrCreateAction", &fn))) {
            mCreateAction = reinterpret_cast<PFN_xrCreateAction>(fn);
        }

        fn = nullptr;
        if (XR_SUCCEEDED(mGetInstanceProcAddr(mInstance, "xrDestroyAction", &fn))) {
            mDestroyAction = reinterpret_cast<PFN_xrDestroyAction>(fn);
        }

        fn = nullptr;
        if (XR_SUCCEEDED(mGetInstanceProcAddr(mInstance, "xrSuggestInteractionProfileBindings", &fn))) {
            mSuggestInteractionProfileBindings = reinterpret_cast<PFN_xrSuggestInteractionProfileBindings>(fn);
        }

        fn = nullptr;
        if (XR_SUCCEEDED(mGetInstanceProcAddr(mInstance, "xrAttachSessionActionSets", &fn))) {
            mAttachSessionActionSets = reinterpret_cast<PFN_xrAttachSessionActionSets>(fn);
        }

        fn = nullptr;
        if (XR_SUCCEEDED(mGetInstanceProcAddr(mInstance, "xrSyncActions", &fn))) {
            mSyncActions = reinterpret_cast<PFN_xrSyncActions>(fn);
        }

        fn = nullptr;
        if (XR_SUCCEEDED(mGetInstanceProcAddr(mInstance, "xrGetActionStateFloat", &fn))) {
            mGetActionStateFloat = reinterpret_cast<PFN_xrGetActionStateFloat>(fn);
        }

        fn = nullptr;
        if (XR_SUCCEEDED(mGetInstanceProcAddr(mInstance, "xrGetActionStateVector2f", &fn))) {
            mGetActionStateVector2f = reinterpret_cast<PFN_xrGetActionStateVector2f>(fn);
        }

        fn = nullptr;
        if (XR_SUCCEEDED(mGetInstanceProcAddr(mInstance, "xrDestroySpace", &fn))) {
            mDestroySpace = reinterpret_cast<PFN_xrDestroySpace>(fn);
        }

        fn = nullptr;
        if (XR_SUCCEEDED(mGetInstanceProcAddr(mInstance, "xrCreateVulkanInstanceKHR", &fn))) {
            mCreateVulkanInstanceKHR = reinterpret_cast<PFN_xrCreateVulkanInstanceKHR>(fn);
        }

        fn = nullptr;
        if (XR_SUCCEEDED(mGetInstanceProcAddr(mInstance, "xrCreateVulkanDeviceKHR", &fn))) {
            mCreateVulkanDeviceKHR = reinterpret_cast<PFN_xrCreateVulkanDeviceKHR>(fn);
        }

        fn = nullptr;
        if (XR_SUCCEEDED(mGetInstanceProcAddr(mInstance, "xrGetVulkanGraphicsDevice2KHR", &fn))) {
            mGetVulkanGraphicsDevice2KHR = reinterpret_cast<PFN_xrGetVulkanGraphicsDevice2KHR>(fn);
        }

        fn = nullptr;
        if (XR_SUCCEEDED(mGetInstanceProcAddr(mInstance, "xrGetVulkanGraphicsRequirements2KHR", &fn))) {
            mGetVulkanGraphicsRequirements2KHR = reinterpret_cast<PFN_xrGetVulkanGraphicsRequirements2KHR>(fn);
        }

        fn = nullptr;
        if (XR_SUCCEEDED(mGetInstanceProcAddr(mInstance, "xrGetVulkanGraphicsRequirementsKHR", &fn))) {
            mGetVulkanGraphicsRequirementsKHR = reinterpret_cast<PFN_xrGetVulkanGraphicsRequirementsKHR>(fn);
        }

        fn = nullptr;
        if (XR_SUCCEEDED(mGetInstanceProcAddr(mInstance, "xrGetVulkanInstanceExtensionsKHR", &fn))) {
            mGetVulkanInstanceExtensionsKHR = reinterpret_cast<PFN_xrGetVulkanInstanceExtensionsKHR>(fn);
        }

        fn = nullptr;
        if (XR_SUCCEEDED(mGetInstanceProcAddr(mInstance, "xrGetVulkanDeviceExtensionsKHR", &fn))) {
            mGetVulkanDeviceExtensionsKHR = reinterpret_cast<PFN_xrGetVulkanDeviceExtensionsKHR>(fn);
        }

        XrSystemGetInfo systemInfo{ XR_TYPE_SYSTEM_GET_INFO };
        systemInfo.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
        XrResult const systemResult = mGetSystem(mInstance, &systemInfo, &mSystemId);
        mLastGetSystemResult = systemResult;
        mSystemAvailable = XR_SUCCEEDED(systemResult);

        if (mGetInstanceProperties) {
            XrInstanceProperties properties{ XR_TYPE_INSTANCE_PROPERTIES };
            if (XR_SUCCEEDED(mGetInstanceProperties(mInstance, &properties))) {
                mRuntimeName = properties.runtimeName;
            }
        }

        if (mStringToPath && mCreateActionSet && mCreateAction && mSuggestInteractionProfileBindings) {
            mStringToPath(mInstance, "/user/hand/left", &mLeftHandPath);
            mStringToPath(mInstance, "/user/hand/right", &mRightHandPath);

            XrActionSetCreateInfo actionSetInfo{ XR_TYPE_ACTION_SET_CREATE_INFO };
            std::strncpy(actionSetInfo.actionSetName, "gameplay", XR_MAX_ACTION_SET_NAME_SIZE - 1);
            std::strncpy(actionSetInfo.localizedActionSetName, "Gameplay", XR_MAX_LOCALIZED_ACTION_SET_NAME_SIZE - 1);
            actionSetInfo.priority = 0;
            if (XR_SUCCEEDED(mCreateActionSet(mInstance, &actionSetInfo, &mInputActionSet))) {
                auto createFloatAction = [&](char const* actionName, char const* localizedName, XrPath subactionPath, XrAction& outAction) {
                    XrActionCreateInfo createInfo{ XR_TYPE_ACTION_CREATE_INFO };
                    createInfo.actionType = XR_ACTION_TYPE_FLOAT_INPUT;
                    std::strncpy(createInfo.actionName, actionName, XR_MAX_ACTION_NAME_SIZE - 1);
                    std::strncpy(createInfo.localizedActionName, localizedName, XR_MAX_LOCALIZED_ACTION_NAME_SIZE - 1);
                    createInfo.countSubactionPaths = 1;
                    createInfo.subactionPaths = &subactionPath;
                    return mCreateAction(mInputActionSet, &createInfo, &outAction);
                };

                auto createVector2Action = [&](char const* actionName, char const* localizedName, XrPath subactionPath, XrAction& outAction) {
                    XrActionCreateInfo createInfo{ XR_TYPE_ACTION_CREATE_INFO };
                    createInfo.actionType = XR_ACTION_TYPE_VECTOR2F_INPUT;
                    std::strncpy(createInfo.actionName, actionName, XR_MAX_ACTION_NAME_SIZE - 1);
                    std::strncpy(createInfo.localizedActionName, localizedName, XR_MAX_LOCALIZED_ACTION_NAME_SIZE - 1);
                    createInfo.countSubactionPaths = 1;
                    createInfo.subactionPaths = &subactionPath;
                    return mCreateAction(mInputActionSet, &createInfo, &outAction);
                };

                if (XR_SUCCEEDED(createFloatAction("move_forward", "Move Forward", mRightHandPath, mMoveForwardAction)) &&
                    XR_SUCCEEDED(createFloatAction("move_backward", "Move Backward", mLeftHandPath, mMoveBackwardAction)) &&
                    XR_SUCCEEDED(createVector2Action("steer", "Steer", mLeftHandPath, mSteerAction)))
                {
                    XrPath oculusTouchProfile = XR_NULL_PATH;
                    mStringToPath(mInstance, "/interaction_profiles/oculus/touch_controller", &oculusTouchProfile);

                    XrPath rightTriggerPath = XR_NULL_PATH;
                    XrPath leftTriggerPath = XR_NULL_PATH;
                    XrPath leftThumbstickPath = XR_NULL_PATH;
                    mStringToPath(mInstance, "/user/hand/right/input/trigger/value", &rightTriggerPath);
                    mStringToPath(mInstance, "/user/hand/left/input/trigger/value", &leftTriggerPath);
                    mStringToPath(mInstance, "/user/hand/left/input/thumbstick", &leftThumbstickPath);

                    XrActionSuggestedBinding bindings[] = {
                        { mMoveForwardAction, rightTriggerPath },
                        { mMoveBackwardAction, leftTriggerPath },
                        { mSteerAction, leftThumbstickPath }
                    };

                    XrInteractionProfileSuggestedBinding suggestedBindings{ XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING };
                    suggestedBindings.interactionProfile = oculusTouchProfile;
                    suggestedBindings.suggestedBindings = bindings;
                    suggestedBindings.countSuggestedBindings = static_cast<uint32_t>(std::size(bindings));
                    mSuggestInteractionProfileBindings(mInstance, &suggestedBindings);
                }
            }
        }

        if (mSystemAvailable && (mVulkanEnable2Available || mVulkanEnableAvailable)) {
            XrGraphicsRequirementsVulkanKHR graphicsRequirements{ XR_TYPE_GRAPHICS_REQUIREMENTS_VULKAN_KHR };
            XrResult graphicsRequirementsResult = XR_ERROR_FUNCTION_UNSUPPORTED;
            if (mVulkanEnable2Available && mGetVulkanGraphicsRequirements2KHR) {
                graphicsRequirementsResult = mGetVulkanGraphicsRequirements2KHR(mInstance, mSystemId, &graphicsRequirements);
            }
            else if (mVulkanEnableAvailable && mGetVulkanGraphicsRequirementsKHR) {
                graphicsRequirementsResult = mGetVulkanGraphicsRequirementsKHR(mInstance, mSystemId, &graphicsRequirements);
            }

            if (XR_SUCCEEDED(graphicsRequirementsResult)) {
                constexpr std::uint32_t kEnginePreferredApi = VK_MAKE_API_VERSION(0, 1, 4, 0);
                constexpr std::uint32_t kMinimumEngineApi = VK_MAKE_API_VERSION(0, 1, 3, 0);
                std::uint32_t const minSupported = ToVkApiVersion(graphicsRequirements.minApiVersionSupported);
                std::uint32_t const maxSupported = ToVkApiVersion(graphicsRequirements.maxApiVersionSupported);

                if (mVulkanEnable2Available) {
                    mVulkanRequirements.apiVersion = kEnginePreferredApi;
                    if (mVulkanRequirements.apiVersion < minSupported) {
                        mVulkanRequirements.apiVersion = minSupported;
                    }
                    if (maxSupported < kMinimumEngineApi) {
                        mStatusText = "OpenXR runtime reported Vulkan max version "
                            + std::to_string(VK_API_VERSION_MAJOR(maxSupported)) + "."
                            + std::to_string(VK_API_VERSION_MINOR(maxSupported)) + "."
                            + std::to_string(VK_API_VERSION_PATCH(maxSupported))
                            + ", but the engine will still try XR_KHR_vulkan_enable2 creation.";
                    }
                }
                else {
                    if (maxSupported < kMinimumEngineApi) {
                        mSystemAvailable = false;
                        mStatusText = "OpenXR runtime Vulkan support is too old for this engine. Runtime max Vulkan version is "
                            + std::to_string(VK_API_VERSION_MAJOR(maxSupported)) + "."
                            + std::to_string(VK_API_VERSION_MINOR(maxSupported)) + "."
                            + std::to_string(VK_API_VERSION_PATCH(maxSupported)) + ".";
                    }
                    else {
                        mVulkanRequirements.apiVersion = kEnginePreferredApi;
                        if (mVulkanRequirements.apiVersion < minSupported) {
                            mVulkanRequirements.apiVersion = minSupported;
                        }
                        if (mVulkanRequirements.apiVersion > maxSupported) {
                            mVulkanRequirements.apiVersion = maxSupported;
                        }
                        if (mVulkanRequirements.apiVersion < kMinimumEngineApi) {
                            mVulkanRequirements.apiVersion = kMinimumEngineApi;
                        }
                    }
                }
            }
        }

        auto appendUniqueExtensions = [](std::vector<std::string>& target, char const* extensionList) {
            if (!extensionList || *extensionList == '\0') {
                return;
            }

            std::istringstream stream(extensionList);
            std::string extension;
            while (std::getline(stream, extension, ' ')) {
                if (extension.empty()) {
                    continue;
                }

                if (std::find(target.begin(), target.end(), extension) == target.end()) {
                    target.emplace_back(std::move(extension));
                }
            }
        };

        if (mSystemAvailable && mGetVulkanInstanceExtensionsKHR) {
            uint32_t requiredSize = 0;
            if (XR_SUCCEEDED(mGetVulkanInstanceExtensionsKHR(mInstance, mSystemId, 0, &requiredSize, nullptr)) && requiredSize > 1) {
                std::string extensions(requiredSize, '\0');
                if (XR_SUCCEEDED(mGetVulkanInstanceExtensionsKHR(mInstance, mSystemId, requiredSize, &requiredSize, extensions.data()))) {
                    appendUniqueExtensions(mVulkanRequirements.instanceExtensions, extensions.c_str());
                }
            }
        }

        if (mSystemAvailable && mGetVulkanDeviceExtensionsKHR) {
            uint32_t requiredSize = 0;
            if (XR_SUCCEEDED(mGetVulkanDeviceExtensionsKHR(mInstance, mSystemId, 0, &requiredSize, nullptr)) && requiredSize > 1) {
                std::string extensions(requiredSize, '\0');
                if (XR_SUCCEEDED(mGetVulkanDeviceExtensionsKHR(mInstance, mSystemId, requiredSize, &requiredSize, extensions.data()))) {
                    appendUniqueExtensions(mVulkanRequirements.deviceExtensions, extensions.c_str());
                }
            }
        }

        RefreshStatus();
        return true;
    }

    bool OpenXrSystem::InitializeSession(
        VkInstance aInstance,
        VkPhysicalDevice aPhysicalDevice,
        VkDevice aDevice,
        std::uint32_t aGraphicsFamilyIndex,
        std::uint32_t aQueueIndex)
    {
        if (!IsAvailable()) {
            return false;
        }

        if ((!mVulkanEnable2Available && !mVulkanEnableAvailable) || !mCreateSession || !mDestroySession) {
            mStatusText = "OpenXR runtime is available, but Vulkan session support functions are missing.";
            return false;
        }

        XrGraphicsRequirementsVulkanKHR graphicsRequirements{ XR_TYPE_GRAPHICS_REQUIREMENTS_VULKAN_KHR };
        XrResult graphicsReqResult = XR_ERROR_FUNCTION_UNSUPPORTED;
        if (mVulkanEnable2Available && mGetVulkanGraphicsRequirements2KHR) {
            graphicsReqResult = mGetVulkanGraphicsRequirements2KHR(mInstance, mSystemId, &graphicsRequirements);
        }
        else if (mVulkanEnableAvailable && mGetVulkanGraphicsRequirementsKHR) {
            graphicsReqResult = mGetVulkanGraphicsRequirementsKHR(mInstance, mSystemId, &graphicsRequirements);
        }
        if (XR_FAILED(graphicsReqResult)) {
            mStatusText = "OpenXR instance created, but xrGetVulkanGraphicsRequirements failed with code " + std::to_string(graphicsReqResult) + ".";
            return false;
        }

        XrGraphicsBindingVulkanKHR graphicsBinding{ XR_TYPE_GRAPHICS_BINDING_VULKAN_KHR };
        graphicsBinding.instance = aInstance;
        graphicsBinding.physicalDevice = aPhysicalDevice;
        graphicsBinding.device = aDevice;
        graphicsBinding.queueFamilyIndex = aGraphicsFamilyIndex;
        graphicsBinding.queueIndex = aQueueIndex;

        XrSessionCreateInfo sessionCreateInfo{ XR_TYPE_SESSION_CREATE_INFO };
        sessionCreateInfo.next = &graphicsBinding;
        sessionCreateInfo.systemId = mSystemId;

        XrResult const sessionResult = mCreateSession(mInstance, &sessionCreateInfo, &mSession);
        if (XR_FAILED(sessionResult)) {
            mStatusText = "OpenXR instance created, but xrCreateSession failed with code " + std::to_string(sessionResult) + ".";
            return false;
        }

        if (mInputActionSet != XR_NULL_HANDLE && mAttachSessionActionSets) {
            XrSessionActionSetsAttachInfo attachInfo{ XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO };
            attachInfo.countActionSets = 1;
            attachInfo.actionSets = &mInputActionSet;
            XrResult const attachResult = mAttachSessionActionSets(mSession, &attachInfo);
            if (XR_FAILED(attachResult)) {
                mStatusText = "OpenXR session created, but xrAttachSessionActionSets failed with code " + std::to_string(attachResult) + ".";
                return false;
            }
        }

        if (!mCreateReferenceSpace || !mDestroySpace) {
            mStatusText = "OpenXR session created, but reference space functions are missing.";
            return false;
        }

        auto tryCreateReferenceSpace = [&](XrReferenceSpaceType spaceType) {
            XrReferenceSpaceCreateInfo referenceSpaceInfo{ XR_TYPE_REFERENCE_SPACE_CREATE_INFO };
            referenceSpaceInfo.referenceSpaceType = spaceType;
            referenceSpaceInfo.poseInReferenceSpace.orientation = { 0.0f, 0.0f, 0.0f, 1.0f };
            referenceSpaceInfo.poseInReferenceSpace.position = { 0.0f, 0.0f, 0.0f };

            return mCreateReferenceSpace(mSession, &referenceSpaceInfo, &mAppSpace);
        };

        XrReferenceSpaceType activeReferenceSpaceType = XR_REFERENCE_SPACE_TYPE_STAGE;
        XrResult referenceSpaceResult = tryCreateReferenceSpace(activeReferenceSpaceType);
        if (XR_FAILED(referenceSpaceResult)) {
            activeReferenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
            referenceSpaceResult = tryCreateReferenceSpace(activeReferenceSpaceType);
        }

        if (XR_FAILED(referenceSpaceResult)) {
            mStatusText =
                "OpenXR session created, but xrCreateReferenceSpace(STAGE/LOCAL) failed with code "
                + std::to_string(referenceSpaceResult) + ".";
            return false;
        }

        if (mEnumerateViewConfigurationViews) {
            uint32_t viewCount = 0;
            XrResult const enumerateResult = mEnumerateViewConfigurationViews(
                mInstance,
                mSystemId,
                XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
                0,
                &viewCount,
                nullptr);

            if (XR_SUCCEEDED(enumerateResult) && viewCount > 0) {
                mViewConfigs.assign(viewCount, { XR_TYPE_VIEW_CONFIGURATION_VIEW });
                mEnumerateViewConfigurationViews(
                    mInstance,
                    mSystemId,
                    XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
                    viewCount,
                    &viewCount,
                    mViewConfigs.data());
                mLocatedViews.assign(viewCount, { XR_TYPE_VIEW });
            }
        }

        if (!CreateSwapchains()) {
            if (mStatusText.empty()) {
                mStatusText = "OpenXR session created, but XR swapchain creation failed.";
            }
            return false;
        }

        mStatusText = "OpenXR session and "
            + std::string(activeReferenceSpaceType == XR_REFERENCE_SPACE_TYPE_STAGE ? "STAGE" : "LOCAL")
            + " reference space created successfully for runtime: " + mRuntimeName;
        return true;
    }

    bool OpenXrSystem::CreateSwapchains()
    {
        if (!mEnumerateSwapchainFormats || !mCreateSwapchainFn || !mDestroySwapchainFn || !mEnumerateSwapchainImages) {
            mStatusText = "OpenXR session created, but swapchain functions are missing.";
            return false;
        }

        if (mViewConfigs.size() < 2) {
            mStatusText = "OpenXR session created, but stereo view configuration data is missing.";
            return false;
        }

        uint32_t formatCount = 0;
        XrResult const formatCountResult = mEnumerateSwapchainFormats(mSession, 0, &formatCount, nullptr);
        if (XR_FAILED(formatCountResult) || formatCount == 0) {
            mStatusText = "xrEnumerateSwapchainFormats failed with code " + std::to_string(formatCountResult) + ".";
            return false;
        }

        std::vector<int64_t> formats(formatCount, 0);
        XrResult const formatListResult = mEnumerateSwapchainFormats(mSession, formatCount, &formatCount, formats.data());
        if (XR_FAILED(formatListResult)) {
            mStatusText = "xrEnumerateSwapchainFormats(list) failed with code " + std::to_string(formatListResult) + ".";
            return false;
        }

        auto supportsFormat = [&](VkFormat format) {
            return std::find(formats.begin(), formats.end(), static_cast<int64_t>(format)) != formats.end();
        };

        VkFormat selectedFormat = VK_FORMAT_UNDEFINED;
        constexpr VkFormat preferredFormats[] = {
            VK_FORMAT_R16G16B16A16_SFLOAT,
            VK_FORMAT_B8G8R8A8_SRGB,
            VK_FORMAT_R8G8B8A8_SRGB,
            VK_FORMAT_B8G8R8A8_UNORM,
            VK_FORMAT_R8G8B8A8_UNORM
        };
        for (VkFormat preferredFormat : preferredFormats) {
            if (supportsFormat(preferredFormat)) {
                selectedFormat = preferredFormat;
                break;
            }
        }

        if (selectedFormat == VK_FORMAT_UNDEFINED) {
            mStatusText = "OpenXR runtime does not expose a compatible Vulkan color swapchain format.";
            return false;
        }

        DestroySwapchains();

        for (std::size_t eyeIndex = 0; eyeIndex < 2; ++eyeIndex) {
            auto& eyeSwapchain = mEyeSwapchains[eyeIndex];
            auto const& viewConfig = mViewConfigs[eyeIndex];

            XrSwapchainCreateInfo createInfo{ XR_TYPE_SWAPCHAIN_CREATE_INFO };
            createInfo.createFlags = 0;
            createInfo.usageFlags =
                XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT |
                XR_SWAPCHAIN_USAGE_SAMPLED_BIT |
                XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT;
            createInfo.format = static_cast<int64_t>(selectedFormat);
            createInfo.sampleCount = 1;
            createInfo.width = std::max<std::uint32_t>(1u, viewConfig.recommendedImageRectWidth);
            createInfo.height = std::max<std::uint32_t>(1u, viewConfig.recommendedImageRectHeight);
            createInfo.faceCount = 1;
            createInfo.arraySize = 1;
            createInfo.mipCount = 1;

            XrResult const createResult = mCreateSwapchainFn(mSession, &createInfo, &eyeSwapchain.handle);
            if (XR_FAILED(createResult)) {
                mStatusText = "xrCreateSwapchain failed with code " + std::to_string(createResult) + ".";
                DestroySwapchains();
                return false;
            }

            eyeSwapchain.format = selectedFormat;
            eyeSwapchain.width = createInfo.width;
            eyeSwapchain.height = createInfo.height;
            eyeSwapchain.acquiredIndex = 0;
            eyeSwapchain.acquired = false;

            uint32_t imageCount = 0;
            XrResult const imageCountResult = mEnumerateSwapchainImages(eyeSwapchain.handle, 0, &imageCount, nullptr);
            if (XR_FAILED(imageCountResult) || imageCount == 0) {
                mStatusText = "xrEnumerateSwapchainImages(count) failed with code " + std::to_string(imageCountResult) + ".";
                DestroySwapchains();
                return false;
            }

            eyeSwapchain.images.assign(imageCount, { XR_TYPE_SWAPCHAIN_IMAGE_VULKAN_KHR });
            XrResult const imageListResult = mEnumerateSwapchainImages(
                eyeSwapchain.handle,
                imageCount,
                &imageCount,
                reinterpret_cast<XrSwapchainImageBaseHeader*>(eyeSwapchain.images.data()));
            if (XR_FAILED(imageListResult)) {
                mStatusText = "xrEnumerateSwapchainImages(list) failed with code " + std::to_string(imageListResult) + ".";
                DestroySwapchains();
                return false;
            }
        }

        return true;
    }

    void OpenXrSystem::DestroySwapchains()
    {
        for (auto& eyeSwapchain : mEyeSwapchains) {
            eyeSwapchain.images.clear();
            eyeSwapchain.acquired = false;
            eyeSwapchain.acquiredIndex = 0;
            eyeSwapchain.width = 0;
            eyeSwapchain.height = 0;
            eyeSwapchain.format = VK_FORMAT_UNDEFINED;

            if (mDestroySwapchainFn && eyeSwapchain.handle != XR_NULL_HANDLE) {
                mDestroySwapchainFn(eyeSwapchain.handle);
            }
            eyeSwapchain.handle = XR_NULL_HANDLE;
        }

        mActiveSwapchainImages = {};
        mSwapchainImagesRendered = false;
    }

    void OpenXrSystem::Shutdown()
    {
        mFrameState = {};
        mSessionRunning = false;
        mSessionBegun = false;
        mFrameBegun = false;
        mPredictedDisplayTime = 0;
        mViewConfigs.clear();
        mLocatedViews.clear();
        DestroySwapchains();

        if (mDestroyAction && mMoveForwardAction != XR_NULL_HANDLE) {
            mDestroyAction(mMoveForwardAction);
        }
        if (mDestroyAction && mMoveBackwardAction != XR_NULL_HANDLE) {
            mDestroyAction(mMoveBackwardAction);
        }
        if (mDestroyAction && mSteerAction != XR_NULL_HANDLE) {
            mDestroyAction(mSteerAction);
        }
        mMoveForwardAction = XR_NULL_HANDLE;
        mMoveBackwardAction = XR_NULL_HANDLE;
        mSteerAction = XR_NULL_HANDLE;

        if (mDestroyActionSet && mInputActionSet != XR_NULL_HANDLE) {
            mDestroyActionSet(mInputActionSet);
        }
        mInputActionSet = XR_NULL_HANDLE;
        mLeftHandPath = XR_NULL_PATH;
        mRightHandPath = XR_NULL_PATH;

        if (mDestroySpace && mAppSpace != XR_NULL_HANDLE) {
            mDestroySpace(mAppSpace);
        }
        mAppSpace = XR_NULL_HANDLE;

        if (mDestroySession && mSession != XR_NULL_HANDLE) {
            mDestroySession(mSession);
        }
        mSession = XR_NULL_HANDLE;

        if (mDestroyInstance && mInstance != XR_NULL_HANDLE) {
            mDestroyInstance(mInstance);
        }

        mInstance = XR_NULL_HANDLE;
        mSystemId = XR_NULL_SYSTEM_ID;
        ReleaseLoader();
        mInitialized = false;
        mWindow = nullptr;
    }

    void OpenXrSystem::PollEvents()
    {
        if (!mPollEvent || mInstance == XR_NULL_HANDLE) {
            return;
        }

        XrEventDataBuffer eventData{ XR_TYPE_EVENT_DATA_BUFFER };
        while (XR_SUCCESS == mPollEvent(mInstance, &eventData)) {
            if (eventData.type == XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED) {
                auto const* stateChanged = reinterpret_cast<XrEventDataSessionStateChanged const*>(&eventData);
                mSessionState = stateChanged->state;

                if (mSessionState == XR_SESSION_STATE_READY && !mSessionBegun && mBeginSession && mSession != XR_NULL_HANDLE) {
                    XrSessionBeginInfo beginInfo{ XR_TYPE_SESSION_BEGIN_INFO };
                    beginInfo.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
                    XrResult const beginResult = mBeginSession(mSession, &beginInfo);
                    if (XR_SUCCEEDED(beginResult)) {
                        mSessionBegun = true;
                        mSessionRunning = true;
                        mStatusText = "OpenXR session began successfully. Waiting for stereo views...";
                    }
                    else {
                        mStatusText = "xrBeginSession failed with code " + std::to_string(beginResult) + ".";
                    }
                }
                else if (mSessionState == XR_SESSION_STATE_STOPPING && mSessionBegun && mEndSession) {
                    mEndSession(mSession);
                    mSessionBegun = false;
                    mSessionRunning = false;
                }
                else if (mSessionState == XR_SESSION_STATE_EXITING || mSessionState == XR_SESSION_STATE_LOSS_PENDING) {
                    mSessionRunning = false;
                }
            }

            eventData = { XR_TYPE_EVENT_DATA_BUFFER };
        }
    }

    bool OpenXrSystem::BeginFrame()
    {
        if (!mEnabled || !IsAvailable() || !mSessionBegun || !mWaitFrame || !mBeginFrameFn || !mLocateViews || mAppSpace == XR_NULL_HANDLE) {
            mFrameState.shouldRender = false;
            mFrameState.hasValidViews = false;
            return false;
        }

        ::XrFrameState xrFrameState{ XR_TYPE_FRAME_STATE };
        XrFrameWaitInfo waitInfo{ XR_TYPE_FRAME_WAIT_INFO };
        XrResult const waitResult = mWaitFrame(mSession, &waitInfo, &xrFrameState);
        if (XR_FAILED(waitResult)) {
            mStatusText = "xrWaitFrame failed with code " + std::to_string(waitResult) + ".";
            mFrameState.shouldRender = false;
            mFrameState.hasValidViews = false;
            return false;
        }

        XrFrameBeginInfo beginInfo{ XR_TYPE_FRAME_BEGIN_INFO };
        XrResult const beginResult = mBeginFrameFn(mSession, &beginInfo);
        if (XR_FAILED(beginResult)) {
            mStatusText = "xrBeginFrame failed with code " + std::to_string(beginResult) + ".";
            mFrameState.shouldRender = false;
            mFrameState.hasValidViews = false;
            return false;
        }

        mFrameBegun = true;
        mPredictedDisplayTime = xrFrameState.predictedDisplayTime;
        mFrameState.shouldRender = xrFrameState.shouldRender == XR_TRUE;
        mFrameState.hasValidViews = false;
        mSwapchainImagesRendered = false;
        mActiveSwapchainImages = {};

        if (!mFrameState.shouldRender || mLocatedViews.size() < 2) {
            return false;
        }

        XrViewState viewState{ XR_TYPE_VIEW_STATE };
        XrViewLocateInfo locateInfo{ XR_TYPE_VIEW_LOCATE_INFO };
        locateInfo.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
        locateInfo.displayTime = xrFrameState.predictedDisplayTime;
        locateInfo.space = mAppSpace;

        uint32_t viewCountOutput = 0;
        XrResult const locateResult = mLocateViews(
            mSession,
            &locateInfo,
            &viewState,
            static_cast<uint32_t>(mLocatedViews.size()),
            &viewCountOutput,
            mLocatedViews.data());

        if (XR_FAILED(locateResult) || viewCountOutput < 2) {
            mStatusText = "xrLocateViews failed with code " + std::to_string(locateResult) + ".";
            return false;
        }

        bool const orientationValid = 0 != (viewState.viewStateFlags & XR_VIEW_STATE_ORIENTATION_VALID_BIT);
        bool const positionValid = 0 != (viewState.viewStateFlags & XR_VIEW_STATE_POSITION_VALID_BIT);
        if (!orientationValid || !positionValid) {
            mStatusText = "OpenXR located views, but pose validity flags are incomplete.";
            return false;
        }

        auto fillEye = [&](StereoEyeView& eyeView, StereoEyeIndex eyeIndex, XrView const& xrView) {
            glm::mat4 worldFromEye = PoseToWorldMatrix(xrView.pose);
            glm::mat4 view = glm::inverse(worldFromEye);
            glm::mat4 projection = ProjectionFromFov(xrView.fov, eyeView.nearPlane, eyeView.farPlane);

            eyeView.eye = eyeIndex;
            eyeView.worldFromEye = worldFromEye;
            eyeView.view = view;
            eyeView.projection = projection;
            eyeView.viewProjection = projection * view;
            eyeView.worldPosition = glm::vec3(worldFromEye[3]);
            eyeView.verticalFovRadians = xrView.fov.angleUp - xrView.fov.angleDown;
        };

        mFrameState.stereoFrame.enabled = true;
        fillEye(mFrameState.stereoFrame.left, StereoEyeIndex::Left, mLocatedViews[0]);
        fillEye(mFrameState.stereoFrame.right, StereoEyeIndex::Right, mLocatedViews[1]);

        if (mAcquireSwapchainImage && mWaitSwapchainImage) {
            for (std::size_t eyeIndex = 0; eyeIndex < 2; ++eyeIndex) {
                auto& eyeSwapchain = mEyeSwapchains[eyeIndex];
                if (eyeSwapchain.handle == XR_NULL_HANDLE || eyeSwapchain.images.empty()) {
                    continue;
                }

                XrSwapchainImageAcquireInfo acquireInfo{ XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO };
                std::uint32_t imageIndex = 0;
                XrResult const acquireResult = mAcquireSwapchainImage(eyeSwapchain.handle, &acquireInfo, &imageIndex);
                if (XR_FAILED(acquireResult) || imageIndex >= eyeSwapchain.images.size()) {
                    mStatusText = "xrAcquireSwapchainImage failed with code " + std::to_string(acquireResult) + ".";
                    return false;
                }

                XrSwapchainImageWaitInfo waitSwapchainInfo{ XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO };
                waitSwapchainInfo.timeout = XR_INFINITE_DURATION;
                XrResult const waitSwapchainResult = mWaitSwapchainImage(eyeSwapchain.handle, &waitSwapchainInfo);
                if (XR_FAILED(waitSwapchainResult)) {
                    mStatusText = "xrWaitSwapchainImage failed with code " + std::to_string(waitSwapchainResult) + ".";
                    return false;
                }

                eyeSwapchain.acquired = true;
                eyeSwapchain.acquiredIndex = imageIndex;
                mActiveSwapchainImages[eyeIndex] = {
                    eyeSwapchain.images[imageIndex].image,
                    eyeSwapchain.width,
                    eyeSwapchain.height,
                    eyeSwapchain.format
                };

                auto& projectionView = mProjectionViews[eyeIndex];
                projectionView = { XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW };
                projectionView.pose = mLocatedViews[eyeIndex].pose;
                projectionView.fov = mLocatedViews[eyeIndex].fov;
                projectionView.subImage.swapchain = eyeSwapchain.handle;
                projectionView.subImage.imageRect.offset = { 0, 0 };
                projectionView.subImage.imageRect.extent = {
                    static_cast<int32_t>(eyeSwapchain.width),
                    static_cast<int32_t>(eyeSwapchain.height)
                };
                projectionView.subImage.imageArrayIndex = 0;
            }
        }

        mFrameState.hasValidViews = true;
        mStatusText = "OpenXR stereo views located successfully.";
        return true;
    }

    void OpenXrSystem::EndFrame()
    {
        if (!mFrameBegun || !mEndFrameFn || mSession == XR_NULL_HANDLE) {
            return;
        }

        bool const submitProjectionLayer =
            mSwapchainImagesRendered &&
            mFrameState.shouldRender &&
            mFrameState.hasValidViews &&
            mEyeSwapchains[0].acquired &&
            mEyeSwapchains[1].acquired;

        if (mReleaseSwapchainImage) {
            for (auto& eyeSwapchain : mEyeSwapchains) {
                if (!eyeSwapchain.acquired || eyeSwapchain.handle == XR_NULL_HANDLE) {
                    continue;
                }

                XrSwapchainImageReleaseInfo releaseInfo{ XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO };
                mReleaseSwapchainImage(eyeSwapchain.handle, &releaseInfo);
                eyeSwapchain.acquired = false;
            }
        }

        if (submitProjectionLayer) {
            mProjectionLayer = { XR_TYPE_COMPOSITION_LAYER_PROJECTION };
            mProjectionLayer.space = mAppSpace;
            mProjectionLayer.viewCount = static_cast<uint32_t>(mProjectionViews.size());
            mProjectionLayer.views = mProjectionViews.data();
            mProjectionLayers[0] = reinterpret_cast<XrCompositionLayerBaseHeader const*>(&mProjectionLayer);
        }

        XrFrameEndInfo endInfo{ XR_TYPE_FRAME_END_INFO };
        endInfo.displayTime = mPredictedDisplayTime;
        endInfo.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
        endInfo.layerCount = submitProjectionLayer ? 1u : 0u;
        endInfo.layers = submitProjectionLayer ? mProjectionLayers.data() : nullptr;
        mEndFrameFn(mSession, &endInfo);
        mFrameBegun = false;
        mSwapchainImagesRendered = false;
        mActiveSwapchainImages = {};
    }

    bool OpenXrSystem::TryGetFrameState(XrFrameState& aOutFrameState) const
    {
        aOutFrameState = mFrameState;
        return aOutFrameState.shouldRender && aOutFrameState.hasValidViews;
    }

    bool OpenXrSystem::TryGetVulkanRequirements(XrVulkanRequirements& aOutRequirements) const
    {
        if (!IsAvailable() || (!mVulkanEnable2Available && !mVulkanEnableAvailable)) {
            return false;
        }

        aOutRequirements = mVulkanRequirements;
        return true;
    }

    bool OpenXrSystem::TryGetSwapchainImages(std::array<XrSwapchainEyeImage, 2>& aOutSwapchainImages) const
    {
        if (!mFrameBegun || !mFrameState.shouldRender || !mFrameState.hasValidViews) {
            return false;
        }

        bool const validImages =
            mActiveSwapchainImages[0].image != VK_NULL_HANDLE &&
            mActiveSwapchainImages[1].image != VK_NULL_HANDLE;
        if (!validImages) {
            return false;
        }

        aOutSwapchainImages = mActiveSwapchainImages;
        return true;
    }

    void OpenXrSystem::ApplyInputState(InputSystem& aInputSystem)
    {
        aInputSystem.ClearInjectedInputs();

        if (!mEnabled || mSession == XR_NULL_HANDLE || !mSessionBegun || !mSyncActions) {
            return;
        }

        XrActiveActionSet activeActionSet{};
        activeActionSet.actionSet = mInputActionSet;

        XrActionsSyncInfo syncInfo{ XR_TYPE_ACTIONS_SYNC_INFO };
        syncInfo.countActiveActionSets = mInputActionSet != XR_NULL_HANDLE ? 1u : 0u;
        syncInfo.activeActionSets = syncInfo.countActiveActionSets > 0 ? &activeActionSet : nullptr;

        if (syncInfo.countActiveActionSets == 0 || XR_FAILED(mSyncActions(mSession, &syncInfo))) {
            return;
        }

        if (mGetActionStateFloat && mMoveForwardAction != XR_NULL_HANDLE) {
            XrActionStateGetInfo getInfo{ XR_TYPE_ACTION_STATE_GET_INFO };
            getInfo.action = mMoveForwardAction;
            getInfo.subactionPath = mRightHandPath;
            XrActionStateFloat state{ XR_TYPE_ACTION_STATE_FLOAT };
            if (XR_SUCCEEDED(mGetActionStateFloat(mSession, &getInfo, &state)) && state.isActive == XR_TRUE) {
                aInputSystem.InjectActionValue("MoveForward", state.currentState);
            }
        }

        if (mGetActionStateFloat && mMoveBackwardAction != XR_NULL_HANDLE) {
            XrActionStateGetInfo getInfo{ XR_TYPE_ACTION_STATE_GET_INFO };
            getInfo.action = mMoveBackwardAction;
            getInfo.subactionPath = mLeftHandPath;
            XrActionStateFloat state{ XR_TYPE_ACTION_STATE_FLOAT };
            if (XR_SUCCEEDED(mGetActionStateFloat(mSession, &getInfo, &state)) && state.isActive == XR_TRUE) {
                aInputSystem.InjectActionValue("MoveBackward", state.currentState);
            }
        }

        if (mGetActionStateVector2f && mSteerAction != XR_NULL_HANDLE) {
            XrActionStateGetInfo getInfo{ XR_TYPE_ACTION_STATE_GET_INFO };
            getInfo.action = mSteerAction;
            getInfo.subactionPath = mLeftHandPath;
            XrActionStateVector2f state{ XR_TYPE_ACTION_STATE_VECTOR2F };
            if (XR_SUCCEEDED(mGetActionStateVector2f(mSession, &getInfo, &state)) && state.isActive == XR_TRUE) {
                if (state.currentState.x < -0.1f) {
                    aInputSystem.InjectActionValue("StrafeLeft", -state.currentState.x);
                }
                else if (state.currentState.x > 0.1f) {
                    aInputSystem.InjectActionValue("StrafeRight", state.currentState.x);
                }
            }
        }
    }

    void OpenXrSystem::MarkSwapchainImagesRendered()
    {
        mSwapchainImagesRendered = true;
    }

    bool OpenXrSystem::CreateVulkanInstanceWithXr(VkInstanceCreateInfo const& aCreateInfo, VkInstance& aOutInstance, VkResult& aOutVkResult)
    {
        if (!mVulkanEnable2Available || !mCreateVulkanInstanceKHR || !IsAvailable()) {
            return false;
        }

        XrVulkanInstanceCreateInfoKHR xrCreateInfo{ XR_TYPE_VULKAN_INSTANCE_CREATE_INFO_KHR };
        xrCreateInfo.systemId = mSystemId;
        xrCreateInfo.createFlags = 0;
        xrCreateInfo.pfnGetInstanceProcAddr = vkGetInstanceProcAddr;
        xrCreateInfo.vulkanCreateInfo = &aCreateInfo;
        xrCreateInfo.vulkanAllocator = nullptr;

        aOutVkResult = VK_SUCCESS;
        XrResult const xrResult = mCreateVulkanInstanceKHR(mInstance, &xrCreateInfo, &aOutInstance, &aOutVkResult);
        if (XR_FAILED(xrResult)) {
            mStatusText = "xrCreateVulkanInstanceKHR failed with code " + std::to_string(xrResult) + ".";
            return false;
        }

        if (VK_SUCCESS != aOutVkResult) {
            mStatusText = "xrCreateVulkanInstanceKHR returned Vulkan error " + std::to_string(aOutVkResult) + ".";
            return false;
        }

        return true;
    }

    bool OpenXrSystem::GetVulkanGraphicsDeviceWithXr(VkInstance aInstance, VkPhysicalDevice& aOutPhysicalDevice)
    {
        if (!mVulkanEnable2Available || !mGetVulkanGraphicsDevice2KHR || !IsAvailable()) {
            return false;
        }

        XrVulkanGraphicsDeviceGetInfoKHR getInfo{ XR_TYPE_VULKAN_GRAPHICS_DEVICE_GET_INFO_KHR };
        getInfo.systemId = mSystemId;
        getInfo.vulkanInstance = aInstance;

        XrResult const xrResult = mGetVulkanGraphicsDevice2KHR(mInstance, &getInfo, &aOutPhysicalDevice);
        if (XR_FAILED(xrResult)) {
            mStatusText = "xrGetVulkanGraphicsDevice2KHR failed with code " + std::to_string(xrResult) + ".";
            return false;
        }

        return true;
    }

    bool OpenXrSystem::CreateVulkanDeviceWithXr(VkPhysicalDevice aPhysicalDevice, VkDeviceCreateInfo const& aCreateInfo, VkDevice& aOutDevice, VkResult& aOutVkResult)
    {
        if (!mVulkanEnable2Available || !mCreateVulkanDeviceKHR || !IsAvailable()) {
            return false;
        }

        XrVulkanDeviceCreateInfoKHR xrCreateInfo{ XR_TYPE_VULKAN_DEVICE_CREATE_INFO_KHR };
        xrCreateInfo.systemId = mSystemId;
        xrCreateInfo.createFlags = 0;
        xrCreateInfo.pfnGetInstanceProcAddr = vkGetInstanceProcAddr;
        xrCreateInfo.vulkanPhysicalDevice = aPhysicalDevice;
        xrCreateInfo.vulkanCreateInfo = &aCreateInfo;
        xrCreateInfo.vulkanAllocator = nullptr;

        aOutVkResult = VK_SUCCESS;
        XrResult const xrResult = mCreateVulkanDeviceKHR(mInstance, &xrCreateInfo, &aOutDevice, &aOutVkResult);
        if (XR_FAILED(xrResult)) {
            mStatusText = "xrCreateVulkanDeviceKHR failed with code " + std::to_string(xrResult) + ".";
            return false;
        }

        if (VK_SUCCESS != aOutVkResult) {
            mStatusText = "xrCreateVulkanDeviceKHR returned Vulkan error " + std::to_string(aOutVkResult) + ".";
            return false;
        }

        return true;
    }

    void OpenXrSystem::SetEnabled(bool aEnabled)
    {
        mEnabled = aEnabled && IsAvailable();
    }

    bool OpenXrSystem::IsEnabled() const
    {
        return mEnabled;
    }

    bool OpenXrSystem::IsAvailable() const
    {
        return mLoaderAvailable && mInstance != XR_NULL_HANDLE && mSystemAvailable;
    }

    bool OpenXrSystem::IsSessionRunning() const
    {
        return mSessionRunning;
    }

    bool OpenXrSystem::IsSessionCreated() const
    {
        return mSession != XR_NULL_HANDLE;
    }

    const char* OpenXrSystem::GetRuntimeName() const
    {
        return mRuntimeName.c_str();
    }

    std::string_view OpenXrSystem::GetStatusText() const
    {
        return mStatusText;
    }

    bool OpenXrSystem::LoadLoaderFunctions()
    {
        auto* loaderModule = LoadLibraryW(L"openxr_loader.dll");
        if (!loaderModule) {
            std::vector<std::filesystem::path> candidatePaths;

            std::wstring const activeRuntimePath = ReadActiveRuntimeRegistryPath();
            if (!activeRuntimePath.empty()) {
                std::filesystem::path runtimePath(activeRuntimePath);
                candidatePaths.emplace_back(runtimePath.parent_path() / "openxr_loader.dll");
                candidatePaths.emplace_back(runtimePath.parent_path() / "bin" / "win64" / "openxr_loader.dll");
            }

            // SteamVR ships a working OpenXR loader even when another runtime is active.
            // Using that loader is fine because it dispatches to the current ActiveRuntime.
            candidatePaths.emplace_back(L"C:\\Program Files (x86)\\Steam\\steamapps\\common\\SteamVR\\bin\\win64\\openxr_loader.dll");

            for (auto const& loaderPath : candidatePaths) {
                if (!std::filesystem::exists(loaderPath)) {
                    continue;
                }

                loaderModule = LoadLibraryW(loaderPath.c_str());
                if (loaderModule) {
                    break;
                }
            }
        }

        if (!loaderModule) {
            return false;
        }

        mLoaderModule = loaderModule;
        mLoaderAvailable = true;

        mGetInstanceProcAddr = reinterpret_cast<PFN_xrGetInstanceProcAddr>(
            GetProcAddress(static_cast<HMODULE>(mLoaderModule), "xrGetInstanceProcAddr"));
        if (!mGetInstanceProcAddr) {
            ReleaseLoader();
            return false;
        }

        mEnumerateInstanceExtensionProperties = reinterpret_cast<PFN_xrEnumerateInstanceExtensionProperties>(
            GetProcAddress(static_cast<HMODULE>(mLoaderModule), "xrEnumerateInstanceExtensionProperties"));

        PFN_xrVoidFunction fn = nullptr;
        if (XR_FAILED(mGetInstanceProcAddr(XR_NULL_HANDLE, "xrCreateInstance", &fn))) {
            ReleaseLoader();
            return false;
        }
        mCreateInstance = reinterpret_cast<PFN_xrCreateInstance>(fn);

        return true;
    }

    void OpenXrSystem::ReleaseLoader()
    {
        mLoaderAvailable = false;
        mSystemAvailable = false;
        mGetInstanceProcAddr = nullptr;
        mEnumerateInstanceExtensionProperties = nullptr;
        mCreateInstance = nullptr;
        mDestroyInstance = nullptr;
        mGetInstanceProperties = nullptr;
        mGetSystem = nullptr;
        mCreateSession = nullptr;
        mDestroySession = nullptr;
        mCreateReferenceSpace = nullptr;
        mDestroySpace = nullptr;
        mStringToPath = nullptr;
        mCreateActionSet = nullptr;
        mDestroyActionSet = nullptr;
        mCreateAction = nullptr;
        mDestroyAction = nullptr;
        mSuggestInteractionProfileBindings = nullptr;
        mAttachSessionActionSets = nullptr;
        mSyncActions = nullptr;
        mGetActionStateFloat = nullptr;
        mGetActionStateVector2f = nullptr;
        mPollEvent = nullptr;
        mBeginSession = nullptr;
        mEndSession = nullptr;
        mWaitFrame = nullptr;
        mBeginFrameFn = nullptr;
        mEndFrameFn = nullptr;
        mLocateViews = nullptr;
        mEnumerateViewConfigurationViews = nullptr;
        mEnumerateSwapchainFormats = nullptr;
        mCreateSwapchainFn = nullptr;
        mDestroySwapchainFn = nullptr;
        mEnumerateSwapchainImages = nullptr;
        mAcquireSwapchainImage = nullptr;
        mWaitSwapchainImage = nullptr;
        mReleaseSwapchainImage = nullptr;
        mCreateVulkanInstanceKHR = nullptr;
        mCreateVulkanDeviceKHR = nullptr;
        mGetVulkanGraphicsDevice2KHR = nullptr;
        mGetVulkanGraphicsRequirements2KHR = nullptr;
        mGetVulkanGraphicsRequirementsKHR = nullptr;
        mGetVulkanInstanceExtensionsKHR = nullptr;
        mGetVulkanDeviceExtensionsKHR = nullptr;
        mVulkanEnableAvailable = false;
        mVulkanEnable2Available = false;
        mVulkanRequirements = {};
        mProjectionLayer = { XR_TYPE_COMPOSITION_LAYER_PROJECTION };
        mProjectionViews = {};
        mProjectionLayers = {};

        if (mLoaderModule) {
            FreeLibrary(static_cast<HMODULE>(mLoaderModule));
            mLoaderModule = nullptr;
        }
    }

    void OpenXrSystem::RefreshStatus()
    {
        if (!mLoaderAvailable) {
            mStatusText = "OpenXR loader DLL was not found. Make sure SteamVR is installed and set as the active OpenXR runtime.";
            if (!mActiveRuntimePath.empty()) {
                mStatusText += " ActiveRuntime: " + mActiveRuntimePath + ".";
            }
            return;
        }

        if (mInstance == XR_NULL_HANDLE) {
            mStatusText = "OpenXR loader detected, but the engine could not create an OpenXR instance";
            if (mLastCreateInstanceResult != XR_SUCCESS) {
                mStatusText += " (code " + std::to_string(mLastCreateInstanceResult) + ")";
            }
            if (!mActiveRuntimePath.empty()) {
                mStatusText += ". ActiveRuntime: " + mActiveRuntimePath;
            }
            mStatusText += ".";
            return;
        }

        if (!mSystemAvailable) {
            if (!mStatusText.empty() && mStatusText.starts_with("OpenXR runtime Vulkan support is too old")) {
                return;
            }

            mStatusText = "OpenXR instance created, but no HMD system was returned";
            if (mLastGetSystemResult != XR_SUCCESS) {
                mStatusText += " (xrGetSystem code " + std::to_string(mLastGetSystemResult) + ")";
            }
            mStatusText += ". Check whether SteamVR is running, the headset is connected through Link/Air Link, and SteamVR is the active OpenXR runtime.";
            return;
        }

        mStatusText = "OpenXR runtime available: " + mRuntimeName;
    }

} // namespace engine
