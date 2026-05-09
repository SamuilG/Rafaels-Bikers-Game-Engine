// VulkanUIRenderer.cpp
// Vulkan UI 渲染器的实现文件，目前为占位实现，仅输出日志
#include "VulkanUIRenderer.hpp"

// 引入引擎 UI 工具类，用于日志输出
#include "../EngineUi.hpp"

namespace engine {

    // 渲染屏幕的占位实现：忽略所有参数，仅打印日志表明该渲染器被调用
    void VulkanUIRenderer::RenderScreen(
        const UIScreen& screen,
        const UIRenderContext& context,
        UIElementId hoveredElementId,
        UIElementId pressedElementId)
    {
        // 当前先保留占位日志，后续真正的 Vulkan 2D/UI 渲染会从这里接入。
        // 抑制未使用参数的编译器警告
        (void)context;
        (void)hoveredElementId;
        (void)pressedElementId;

        // 输出占位日志，记录当前渲染的屏幕名称
        EngineUi::LogPrint(
            "[VulkanUIRenderer] Placeholder render for screen '{}'\n",
            screen.GetName());
    }

} // namespace engine
