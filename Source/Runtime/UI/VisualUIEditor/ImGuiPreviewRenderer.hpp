#pragma once

#include <functional>
#include <string>

#include "UIRenderer.hpp"

namespace engine {

    // 基于 ImGui DrawList 的预览渲染器。
    // 既用于 Game UI Editor 画布预览，也作为当前运行时 UI 的【临时】实现。
    //
    
    class ImGuiPreviewRenderer final : public UIRenderer {
    public:
        // 贴图解析器由 RenderSystem 注入，内部复用引擎现有的缩略图 / descriptor 管线。
        void SetTextureResolver(std::function<void*(const std::string&)> resolver);

        // 渲染整张 UIScreen，递归遍历所有 UI 元素并绘制到 ImGui DrawList。
        void RenderScreen(
            const UIScreen& screen,
            const UIRenderContext& context,
            UIElementId hoveredElementId,
            UIElementId pressedElementId) override;

    private:
        // 贴图解析回调，将资源路径映射为 ImTextureID（由外部注入）。
        std::function<void*(const std::string&)> mTextureResolver;
    };

} // namespace engine
