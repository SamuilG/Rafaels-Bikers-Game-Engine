#pragma once

#include <memory>

#include "UIScreen.hpp"

namespace engine {

    // 与具体绘制后端解耦的运行时渲染上下文。
    // rootPosition / targetSize / scale 负责把参考分辨率映射到实际视口。
    struct UIRenderContext {
        glm::vec2 rootPosition = glm::vec2(0.0f);
        glm::vec2 targetSize = glm::vec2(1920.0f, 1080.0f);
        glm::vec2 axisScale = glm::vec2(1.0f, 1.0f);
        float scale = 1.0f;
        float opacityMultiplier = 1.0f;
        float hudOpacityMultiplier = 1.0f;
        float speedTextScale = 1.0f;
        UIElementId selectedElementId = 0;
        void* nativeContext = nullptr;
    };

    // 运行时 UI 渲染接口。
    // 当前编辑器预览使用 ImGuiPreviewRenderer，未来运行时可替换为 VulkanUIRenderer。
    class UIRenderer {
    public:
        virtual ~UIRenderer() = default;

        virtual void RenderScreen(
            const UIScreen& screen,
            const UIRenderContext& context,
            UIElementId hoveredElementId,
            UIElementId pressedElementId) = 0;
    };

    using UIRendererPtr = std::shared_ptr<UIRenderer>;

} // namespace engine
