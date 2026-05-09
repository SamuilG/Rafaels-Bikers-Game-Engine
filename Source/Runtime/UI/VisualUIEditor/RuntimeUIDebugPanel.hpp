#pragma once

#include <string>

#include <glm/glm.hpp>
#include <imgui.h>

#include "UIBinding.hpp"
#include "UICommon.hpp"
#include "UIRenderer.hpp"

namespace engine {

    class UIManager;
    class UIScreen;
    class UIElement;
    struct UserState;

    
    namespace RuntimeUIDebugPanel {

        // 调试面板自带的开关 / 选中状态。RenderSystem 持有一份实例，
        // 通过下面的渲染函数转发 —— 这里没有 ImGui 调用，方便序列化。
        struct DebugState {
            UIElementId selectedElementId = 0;       // 在视口里点选到的元素 ID
            std::string selectedScreenName;          // 选中元素所属屏幕名

            bool enablePicking = true;                // 视口点击是否拾取运行时元素
            bool showLivePreview = false;             // 在编辑器模式下叠加显示运行时 UI
            bool showBounds = false;                  // 元素包围盒
            bool showHitRects = false;                // 命中矩形（可交互元素的红框）
            bool showBindingValues = true;            // 选中元素附近显示绑定值
            bool showAnimationDebug = false;          // 选中元素附近显示活动动画
            bool showEventLog = true;                 // 显示最近 UI 事件日志
            bool showDataContext = true;              // 显示数据上下文全部键值
        };

        // 主调试面板（ImGui 窗口） —— 显示活跃屏幕、游戏流程、数据上下文、
        // 活动动画、最近触发事件等。
        void DrawDebugPanel(DebugState& state,
                            UIManager& uiManager,
                            const UserState& userState);

        // 视口内的调试覆盖层：包围盒、命中矩形、选中高亮、绑定值小标签。
        // 调用前必须确保 BuildRuntimeUiRenderContext 已经成功生成 context。
        void DrawDebugOverlay(const DebugState& state,
                              const UIManager& uiManager,
                              const UIRenderContext& context,
                              ImVec2 canvasMin,
                              ImVec2 canvasMax);

        // 在视口左上角绘制 "Runtime UI Stack" 列表，可选地叠加 HUD DataContext 速览。
        void DrawRuntimeStackOverlay(const UIManager& uiManager,
                                     const UIRenderContext& context,
                                     ImVec2 canvasMin,
                                     ImVec2 canvasMax,
                                     bool showHudDataContext);

        // 视口左键拾取 —— 把鼠标坐标映射到运行时空间并写回 selectedElementId。
        // 返回 true 表示这一帧的左键被拾取消费，外部不应当再做实体拾取。
        bool HandleViewportPick(DebugState& state,
                                const UIManager& uiManager,
                                const UIRenderContext& context,
                                ImVec2 canvasMin,
                                ImVec2 canvasMax,
                                ImVec2 mousePosition);

        // 调试用：把任意 UIValue 渲染为简短可读字符串。多处共用。
        std::string FormatUiValueForDebug(const UIValue* value);

    } // namespace RuntimeUIDebugPanel

} // namespace engine
