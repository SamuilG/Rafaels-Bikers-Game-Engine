#include "ui.hpp"

#include <stdexcept>
#include <array>

#include <GLFW/glfw3.h>

#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_vulkan.h>


ImGuiRenderer imguiRenderer;

void ImGuiRenderer::DefaultCheck(VkResult err)
{
    if (err != VK_SUCCESS)
        throw std::runtime_error("ImGui Vulkan backend VkResult error.");
}

void ImGuiRenderer::CreateDescriptorPool()
{
    std::array<VkDescriptorPoolSize, 11> sizes = { {
        { VK_DESCRIPTOR_TYPE_SAMPLER, 1000 },
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000 },
        { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000 },
        { VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000 },
    } };

    VkDescriptorPoolCreateInfo ci{ VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
    ci.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    ci.maxSets = 1000u * (uint32_t)sizes.size();
    ci.poolSizeCount = (uint32_t)sizes.size();
    ci.pPoolSizes = sizes.data();

    if (vkCreateDescriptorPool(m_device, &ci, nullptr, &m_pool) != VK_SUCCESS) {
        throw std::runtime_error("vkCreateDescriptorPool(ImGui) failed");
    }
}

void ImGuiRenderer::Init(const InitInfo& info)
{
    if (m_inited) return;

    if (!info.window) throw std::runtime_error("ImGuiRenderer::Init window null");
    if (info.device == VK_NULL_HANDLE) throw std::runtime_error("ImGuiRenderer::Init device null");
    if (info.colorFormat == VK_FORMAT_UNDEFINED) throw std::runtime_error("ImGuiRenderer::Init colorFormat undefined");
    if (info.imageCount < 2) throw std::runtime_error("ImGuiRenderer::Init imageCount must be >=2");

    m_device = info.device;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();

	//================Font settings字体设置=============================
    // 调整全局缩放比例
    //io.FontGlobalScale = 1.f; //Font Scale 字体比例

	// load font加载字体
    //io.Fonts->AddFontFromFileTTF("Assets/Fonts/BRADHITC.TTF", 16.0f);

    // load Chinese Font 加载中文字体：
    io.Fonts->AddFontFromFileTTF("Assets/Fonts/simhei.ttf", 18.0f, NULL, io.Fonts->GetGlyphRangesChineseFull());
    //================Font settings字体设置=============================
    
	//================Layout settings布局设置============================
    //ImGui::StyleColorsLight(); 
    //ImGui::StyleColorsClassic();
    //ImGui::StyleColorsDark();

    //自定义全局样式
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 28.0f;       // 窗口圆角 Rounded corners of windows
    style.FrameRounding = 4.0f;        // 按钮圆角 Button rounded corners
    style.Alpha = 0.8f;               // 全局透明 Transparency
    style.Colors[ImGuiCol_WindowBg] = ImVec4(0.f, 2.1f, 2.1f, 0.9f); // Background color自定义背景色
    //================Layout settings布局设置============================

    // Use the Tab and arrow keys to navigate between UI buttons.
    // Tab 和方向键来在 UI 按钮之间导航
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    ImGui_ImplGlfw_InitForVulkan(info.window, true);


    CreateDescriptorPool();

    static VkFormat s_colorFmt;
    s_colorFmt = info.colorFormat;

    static VkPipelineRenderingCreateInfo s_pri{ VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO };
    s_pri.colorAttachmentCount = 1;
    s_pri.pColorAttachmentFormats = &s_colorFmt;
    s_pri.depthAttachmentFormat = info.depthFormat;
    s_pri.stencilAttachmentFormat = VK_FORMAT_UNDEFINED;

    ImGui_ImplVulkan_InitInfo ii{};
    ii.ApiVersion = VK_API_VERSION_1_3;
    ii.Instance = info.instance;
    ii.PhysicalDevice = info.physicalDevice;
    ii.Device = info.device;
    ii.QueueFamily = info.queueFamily;
    ii.Queue = info.queue;
    ii.DescriptorPool = m_pool;
    ii.MinImageCount = info.imageCount;
    ii.ImageCount = info.imageCount;
    ii.UseDynamicRendering = true;
    ii.CheckVkResultFn = info.checkVkResultFn ? info.checkVkResultFn : &ImGuiRenderer::DefaultCheck;
    ii.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    ii.PipelineInfoMain.PipelineRenderingCreateInfo = s_pri;

    if (!ImGui_ImplVulkan_Init(&ii))
    {
        throw std::runtime_error("ImGui_ImplVulkan_Init failed");
    }

    m_inited = true;
}

void ImGuiRenderer::Shutdown()
{
    if (!m_inited) return;

    vkDeviceWaitIdle(m_device);

    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    if (m_pool != VK_NULL_HANDLE)
    {
        vkDestroyDescriptorPool(m_device, m_pool, nullptr);
        m_pool = VK_NULL_HANDLE;
    }

    m_inited = false;
}

void ImGuiRenderer::BeginFrame()
{
    if (!m_inited) return;
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
}

void ImGuiRenderer::BuildDemoUI()
{
    if (!m_inited) return;
    ImGui::ShowDemoWindow();
}

void ImGuiRenderer::Render(VkCommandBuffer cmd)
{
    if (!m_inited) return;
    ImGui::Render();
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);
}