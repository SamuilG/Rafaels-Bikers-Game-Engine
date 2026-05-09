#pragma once


#include "UIRenderer.hpp"

namespace engine {


    class VulkanUIRenderer final : public UIRenderer {
    public:
        // 渲染指定的 UI 屏幕，接收悬停和按下的元素 ID 用于交互状态处理
        void RenderScreen(
            const UIScreen& screen,
            const UIRenderContext& context,
            UIElementId hoveredElementId,
            UIElementId pressedElementId) override;
    };

} // namespace engine
