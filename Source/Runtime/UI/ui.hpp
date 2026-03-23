#pragma once

#include <volk/volk.h>

struct GLFWwindow;

class ImGuiRenderer
{
public:
    struct InitInfo
    {
        GLFWwindow* window = nullptr;

        VkInstance instance = VK_NULL_HANDLE;
        VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
        VkDevice device = VK_NULL_HANDLE;

        uint32_t queueFamily = 0;
        VkQueue queue = VK_NULL_HANDLE;

        VkFormat colorFormat = VK_FORMAT_UNDEFINED;
        VkFormat depthFormat = VK_FORMAT_UNDEFINED;
        uint32_t imageCount = 2;

        void (*checkVkResultFn)(VkResult) = nullptr;
    };

    void Init(const InitInfo& info);
    void Shutdown();

    void BeginFrame();
    void BuildDemoUI();
    void Render(VkCommandBuffer cmd);

private:
    VkDevice m_device = VK_NULL_HANDLE;
    VkDescriptorPool m_pool = VK_NULL_HANDLE;
    bool m_inited = false;

    static void DefaultCheck(VkResult err);
    void CreateDescriptorPool();
};

extern ImGuiRenderer imguiRenderer;