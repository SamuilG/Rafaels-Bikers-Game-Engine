#include "ui.hpp"

#include <stdexcept>
#include <array>

#include <GLFW/glfw3.h>

#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_vulkan.h>
#include <string_view>

//============== = Global Instance全局实例============================
ImGuiRenderer imguiRenderer;

//================Vulkan Function Loader驱动函数加载===================
static PFN_vkVoidFunction __cdecl MyVulkanLoader(const char* function_name, void* user_data) {
    std::string_view name(function_name);

    if (name == "vkCmdBeginRenderingKHR") return (PFN_vkVoidFunction)vkCmdBeginRenderingKHR;
    if (name == "vkCmdBeginRendering")    return (PFN_vkVoidFunction)vkCmdBeginRendering;
    if (name == "vkCmdEndRenderingKHR")   return (PFN_vkVoidFunction)vkCmdEndRenderingKHR;
    if (name == "vkCmdEndRendering")      return (PFN_vkVoidFunction)vkCmdEndRendering;

    VkInstance instance = *(VkInstance*)user_data;
    return (PFN_vkVoidFunction)glfwGetInstanceProcAddress(instance, function_name);
}

void ImGuiRenderer::DefaultCheck(VkResult err)
{
    if (err != VK_SUCCESS)
        throw std::runtime_error("ImGui Vulkan backend VkResult error.");
}

//================Descriptor Pool创建描述符池=========================
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

//================System Initialization初始化========================
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
    // load Chinese Font 加载中文字体：
    io.Fonts->AddFontFromFileTTF("Assets/Fonts/simhei.ttf", 18.0f, NULL, io.Fonts->GetGlyphRangesChineseFull());

    //================Layout settings布局设置============================
    //ImGui::StyleColorsLight();
	ImGui::StyleColorsDark();//暗色主题

    ImGuiStyle& style = ImGui::GetStyle();
	style.WindowRounding = 8.0f;// 窗口圆角//window rounding
	style.FrameRounding = 4.0f;// 控件圆角//rounding
	style.Alpha = 0.95f;// 整体透明度//opacity
    
    //================Features功能开关==================================
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;// 开启键盘导航
	
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;// 开启 Docking (停靠系统)// Enable Docking

    //================Backend Init后端初始化=============================
	ImGui_ImplGlfw_InitForVulkan(info.window, true);// 初始化 GLFW 后端// Initialize GLFW backend

	CreateDescriptorPool();// 创建描述符池// Create Descriptor Pool

    static VkFormat s_colorFmt;
    s_colorFmt = info.colorFormat;

    static VkPipelineRenderingCreateInfo s_pri{ VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO };
    s_pri.colorAttachmentCount = 1;
    s_pri.pColorAttachmentFormats = &s_colorFmt;
    s_pri.depthAttachmentFormat = info.depthFormat;
    s_pri.stencilAttachmentFormat = VK_FORMAT_UNDEFINED;

	// 初始化 Vulkan 后端信息// Initialize Vulkan backend info
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
	ii.UseDynamicRendering = true;// 强制使用动态渲染管线// Force using dynamic rendering pipeline
    ii.CheckVkResultFn = info.checkVkResultFn ? info.checkVkResultFn : &ImGuiRenderer::DefaultCheck;
    ii.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    ii.PipelineInfoMain.PipelineRenderingCreateInfo = s_pri;

	// 加载 Vulkan 函数指针// Load Vulkan function pointers
    ImGui_ImplVulkan_LoadFunctions(ii.ApiVersion, MyVulkanLoader, &ii.Instance);

    if (!ImGui_ImplVulkan_Init(&ii))
    {
        throw std::runtime_error("ImGui_ImplVulkan_Init failed");
    }

    m_inited = true;
}

//================UI System Shutdown===========================
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

//================Frame Lifecycle帧生命周期==========================
void ImGuiRenderer::BeginFrame()
{
    if (!m_inited) return;
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
}

//demo
void ImGuiRenderer::BuildDemoUI()
{
    if (!m_inited) return;
    ImGui::ShowDemoWindow();
}

//================Rendering指令录制================================
void ImGuiRenderer::Render(VkCommandBuffer cmd)
{
    if (!m_inited) return;
    ImGui::Render();
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);
}