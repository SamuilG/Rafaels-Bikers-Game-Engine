#include "GameUi.hpp"

#include "../Renderer/RenderSystem.hpp"
#include "../UserState/UserState.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

#include <imgui.h>

namespace engine {
    namespace {
        enum class UiAnchor {
            TopLeft,
            TopCenter,
            TopRight,
            CenterLeft,
            Center,
            CenterRight,
            BottomLeft,
            BottomCenter,
            BottomRight
        };

        struct SpeedometerDemoStyle {
            // =========================
            // Core enable / asset setup
            // =========================
            bool enabled = true; // Master switch for this entire demo speedometer.
            const char* backgroundTexturePath = "Assets/Textures/speed_ui.png"; // Replace with your real speedometer background PNG.
            bool useBackgroundImage = true; // false = draw a colored debug panel instead of using a texture.

            // =========================
            // Position / anchoring
            // =========================
            UiAnchor anchor = UiAnchor::BottomCenter; // Current example: bottom-center of the game viewport.
            ImVec2 alignment = ImVec2(0.5f, 1.0f); // Pivot inside the widget: (0,0)=top-left, (0.5,0.5)=center, (1,1)=bottom-right.
            ImVec2 offset = ImVec2(0.0f, -28.0f); // Extra offset after anchor/alignment is resolved.

            // Quick examples:
            // anchor = UiAnchor::TopLeft;     alignment = ImVec2(0.0f, 0.0f); // 左上角对齐
            // anchor = UiAnchor::Center;      alignment = ImVec2(0.5f, 0.5f); // 正中间居中
            // anchor = UiAnchor::BottomRight; alignment = ImVec2(1.0f, 1.0f); // 右下角贴边

            // =========================
            // Background image sizing
            // =========================
            ImVec2 backgroundSize = ImVec2(320.0f, 160.0f); // Final draw size in pixels.
            float backgroundScale = 1.0f; // Multiplies backgroundSize.
            ImVec4 backgroundTint = ImVec4(1.0f, 1.0f, 1.0f, 0.92f); // Tint and alpha for the background image.
            float fallbackPanelRounding = 18.0f; // Rounding when no image is available.
            ImVec4 fallbackPanelColor = ImVec4(0.08f, 0.12f, 0.16f, 0.82f); // Debug fallback color.
            ImVec4 debugBoundsColor = ImVec4(1.0f, 0.2f, 0.2f, 0.9f); // Only used when drawDebugBounds=true.

            // Optional, currently unused examples:
            // bool flipBackgroundX = false; // Mirror the background horizontally. Requires custom UV handling.
            // bool flipBackgroundY = false; // Mirror the background vertically. Requires custom UV handling.
            // float backgroundRotationDeg = 0.0f; // Rotate background. Requires AddImageQuad()/manual quad math.
            // ImVec2 backgroundUvMin = ImVec2(0.0f, 0.0f); // Crop / remap texture UV min.
            // ImVec2 backgroundUvMax = ImVec2(1.0f, 1.0f); // Crop / remap texture UV max.

            // =========================
            // Speed conversion / animation
            // =========================
            float speedMultiplier = 3.6f; // Convert current internal speed to km/h. Use 2.23694f for mph.
            float maxDisplaySpeed = 180.0f; // Only affects percent/animation logic, not clamping the printed number.
            float smoothingFactor = 0.12f; // 0 = no motion, 1 = instant update. Higher = snappier numbers.
            bool useSmoothing = true; // false = number instantly follows the real speed.
            bool pulseWhenFast = true; // Adds a subtle scale pulse at high speed.
            float pulseStartSpeed = 80.0f; // Start pulsing above this displayed speed.
            float pulseAmplitude = 0.045f; // How much the whole widget scales while pulsing.
            float pulseSpeed = 6.0f; // Pulse frequency.

            // Optional, currently unused examples:
            // bool clampPrintedSpeed = false; // true = limit the shown number to maxDisplaySpeed.
            // float fakeTestSpeed = 120.0f; // Useful when previewing UI without gameplay data.
            // bool overrideSpeedForPreview = false; // true = use fakeTestSpeed instead of state.bikeSpeed.

            // =========================
            // Big speed number
            // =========================
            float numberFontSize = 46.0f; // Pixel font size for the main number.
            float numberFontScale = 1.0f; // Extra multiplier on numberFontSize.
            ImVec2 numberOffset = ImVec2(0.0f, 58.0f); // Position inside the background image.
            ImVec4 numberColor = ImVec4(0.96f, 0.98f, 1.0f, 1.0f); // Main number color.
            int decimals = 0; // 0 = integer speed, 1 = one decimal place, etc.
            bool padWithZeros = false; // true = 005, 042, 120 style.
            int minimumIntegerDigits = 3; // Used only when padWithZeros=true.
            bool centerNumber = true; // true = center around numberOffset.x, false = draw from that point directly.

            // Optional font usage examples:
            // ImFont* customNumberFont = nullptr; // Set this if you later build a dedicated speed font.
            // float customNumberFontSize = 52.0f; // Size for the custom font draw call.

            // =========================
            // Unit label (m/s)
            // =========================
            bool showUnitLabel = true;
            const char* unitLabel = "m/s";
            float unitFontSize = 20.0f;
            float unitFontScale = 1.0f;
            ImVec2 unitOffset = ImVec2(0.0f, 105.0f);
            ImVec4 unitColor = ImVec4(0.78f, 0.84f, 0.92f, 0.98f);
            bool centerUnit = true;

            // =========================
            // Extra labels
            // =========================
            bool showTitle = true;
            const char* titleText = "SPEED";
            float titleFontSize = 18.0f;
            ImVec2 titleOffset = ImVec2(0.0f, 24.0f);
            ImVec4 titleColor = ImVec4(0.88f, 0.91f, 0.96f, 0.95f);
            bool centerTitle = true;

            // =========================
            // Text effects
            // =========================
            bool drawTextShadow = true; // Draw a soft shadow behind all labels.
            ImVec2 shadowOffset = ImVec2(2.0f, 2.0f); // Shadow offset in pixels.
            ImVec4 shadowColor = ImVec4(0.02f, 0.03f, 0.05f, 0.85f); // Shadow color/alpha.

            bool drawTextOutline = true; // Draw a simple 4-direction outline for readability.
            float outlineThickness = 1.0f; // Outline offset distance.
            ImVec4 outlineColor = ImVec4(0.02f, 0.03f, 0.05f, 0.95f); // Outline color.

            // Optional, currently unused examples:
            // bool useGlow = false; // Draw multiple expanded passes for glow.
            // ImVec4 glowColor = ImVec4(0.2f, 0.7f, 1.0f, 0.35f); // Glow color.
            // float glowRadius = 6.0f; // Glow spread in pixels.

            // =========================
            // Layout / clipping / debug
            // =========================
            bool clipToViewport = true; // Prevent drawing outside the game viewport.
            bool drawDebugBounds = false; // Draw the widget rectangle for layout debugging.
            float safeMargin = 8.0f; // Keeps the widget slightly inside the viewport bounds.

            // Optional, currently unused examples:
            // bool keepInsideViewport = true; // Clamp the final widget rect fully inside the viewport.
            // bool snapToIntegerPixels = true; // Helpful for crisp UI when scaling pixel-art textures.
            // ImVec2 manualTopLeft = ImVec2(100.0f, 100.0f); // Bypass anchor logic and place directly.
        };

        ImVec2 ResolveAnchorPoint(UiAnchor anchor, const ImVec2& viewportPos, const ImVec2& viewportSize) {
            switch (anchor) {
            case UiAnchor::TopLeft:
                return viewportPos;
            case UiAnchor::TopCenter:
                return ImVec2(viewportPos.x + viewportSize.x * 0.5f, viewportPos.y);
            case UiAnchor::TopRight:
                return ImVec2(viewportPos.x + viewportSize.x, viewportPos.y);
            case UiAnchor::CenterLeft:
                return ImVec2(viewportPos.x, viewportPos.y + viewportSize.y * 0.5f);
            case UiAnchor::Center:
                return ImVec2(viewportPos.x + viewportSize.x * 0.5f, viewportPos.y + viewportSize.y * 0.5f);
            case UiAnchor::CenterRight:
                return ImVec2(viewportPos.x + viewportSize.x, viewportPos.y + viewportSize.y * 0.5f);
            case UiAnchor::BottomLeft:
                return ImVec2(viewportPos.x, viewportPos.y + viewportSize.y);
            case UiAnchor::BottomCenter:
                return ImVec2(viewportPos.x + viewportSize.x * 0.5f, viewportPos.y + viewportSize.y);
            case UiAnchor::BottomRight:
                return ImVec2(viewportPos.x + viewportSize.x, viewportPos.y + viewportSize.y);
            default:
                return viewportPos;
            }
        }

        ImU32 ToU32(const ImVec4& color) {
            return ImGui::ColorConvertFloat4ToU32(color);
        }

        ImVec2 CalculateTextPosition(ImFont* font, float fontSize, const char* text, const ImVec2& anchorPoint, bool centerText) {
            if (!font || !text) {
                return anchorPoint;
            }

            if (!centerText) {
                return anchorPoint;
            }

            ImVec2 textSize = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, text);
            return ImVec2(anchorPoint.x - textSize.x * 0.5f, anchorPoint.y - textSize.y * 0.5f);
        }

        void DrawStyledText(ImDrawList* drawList, ImFont* font, float fontSize, const ImVec2& position, ImU32 color, const char* text, const SpeedometerDemoStyle& style) {
            if (!drawList || !font || !text) {
                return;
            }

            if (style.drawTextShadow) {
                drawList->AddText(
                    font,
                    fontSize,
                    ImVec2(position.x + style.shadowOffset.x, position.y + style.shadowOffset.y),
                    ToU32(style.shadowColor),
                    text);
            }

            if (style.drawTextOutline) {
                const float t = style.outlineThickness;
                const ImU32 outline = ToU32(style.outlineColor);
                const std::array<ImVec2, 4> outlineOffsets = {
                    ImVec2(-t, 0.0f),
                    ImVec2(t, 0.0f),
                    ImVec2(0.0f, -t),
                    ImVec2(0.0f, t)
                };

                for (const ImVec2& offset : outlineOffsets) {
                    drawList->AddText(font, fontSize, ImVec2(position.x + offset.x, position.y + offset.y), outline, text);
                }
            }

            drawList->AddText(font, fontSize, position, color, text);
        }

        SpeedometerDemoStyle BuildSpeedometerDemoStyle() {
            SpeedometerDemoStyle style{};

            // ==========================================
            // 教学用配置区：
            // 以后你要改风格，优先改这里。
            // ==========================================

            style.enabled = true;
            style.backgroundTexturePath = "Assets/Textures/UiSpeed.png"; // 速度表背景图，直接替换成你自己的 PNG。
            style.useBackgroundImage = true; // false 时会用纯色面板占位，方便调布局。

            style.anchor = UiAnchor::BottomCenter; // 当前示例：底部居中。
            style.alignment = ImVec2(0.5f, 1.0f); // 当前示例：以速度表底边中心作为对齐点。
            style.offset = ImVec2(0.0f, -28.0f); // 往上抬一点，避免太贴底。

            // 下面这些是常见对齐示例，保留作参考：
            // style.anchor = UiAnchor::TopLeft;     style.alignment = ImVec2(0.0f, 0.0f); // 左上角对齐
            // style.anchor = UiAnchor::TopRight;    style.alignment = ImVec2(1.0f, 0.0f); // 右上角对齐
            // style.anchor = UiAnchor::Center;      style.alignment = ImVec2(0.5f, 0.5f); // 整个速度表居中
            // style.anchor = UiAnchor::BottomLeft;  style.alignment = ImVec2(0.0f, 1.0f); // 左下角贴边

            style.backgroundSize = ImVec2(320.0f, 160.0f); // 速度表整体尺寸。
            style.backgroundScale = 1.0f; // 想整体放大就改这里。
            style.backgroundTint = ImVec4(1.0f, 1.0f, 1.0f, 0.92f); // 改 alpha 可以让底图更透明。
            style.fallbackPanelRounding = 18.0f; // 没图时的圆角。

            style.speedMultiplier = 3.6f; // 当前假设内部速度是 m/s，所以转 km/h。
            style.maxDisplaySpeed = 180.0f; // 主要用于百分比/动画强度。
            style.useSmoothing = true; // 开启动画平滑。
            style.smoothingFactor = 0.12f; // 越大越跟手，越小越“有惯性”。
            style.pulseWhenFast = true; // 高速时轻微脉冲。
            style.pulseStartSpeed = 80.0f;
            style.pulseAmplitude = 0.045f;
            style.pulseSpeed = 6.0f;

            style.showTitle = true;
            style.titleText = "SPEED";
            style.titleFontSize = 18.0f;
            style.titleOffset = ImVec2(0.0f, 24.0f);

            style.numberFontSize = 46.0f; // 主数字大小。
            style.numberFontScale = 1.0f; // 二次缩放。
            style.numberOffset = ImVec2(0.0f, 64.0f); // 数字在背景图里的位置。
            style.decimals = 0; // 显示整数速度。
            style.padWithZeros = false; // true 会显示成 005/042/120。
            style.minimumIntegerDigits = 3;

            style.showUnitLabel = true;
            style.unitLabel = "km/h";
            style.unitFontSize = 20.0f;
            style.unitOffset = ImVec2(0.0f, 108.0f);

            style.drawTextShadow = true; // 阴影增强可读性。
            style.shadowOffset = ImVec2(2.0f, 2.0f);
            style.drawTextOutline = true; // 描边增强亮背景下的可读性。
            style.outlineThickness = 1.0f;

            style.clipToViewport = true; // 不让 UI 画出游戏画面外。
            style.drawDebugBounds = false; // 调布局时可改成 true 看边框。
            style.safeMargin = 8.0f;

            return style;
        }

        void DrawImageSpeedometerDemo(RenderSystem* renderSys, const UserState& state, const ImVec2& viewportPos, const ImVec2& viewportSize) {
            if (!renderSys || viewportSize.x <= 1.0f || viewportSize.y <= 1.0f) {
                return;
            }

            const SpeedometerDemoStyle style = BuildSpeedometerDemoStyle();
            if (!style.enabled) {
                return;
            }

            const ImVec2 widgetSize(
                style.backgroundSize.x * style.backgroundScale,
                style.backgroundSize.y * style.backgroundScale);

            const ImVec2 anchorPoint = ResolveAnchorPoint(style.anchor, viewportPos, viewportSize);
            ImVec2 widgetMin(
                anchorPoint.x - widgetSize.x * style.alignment.x + style.offset.x,
                anchorPoint.y - widgetSize.y * style.alignment.y + style.offset.y);

            if (style.safeMargin > 0.0f) {
                const float minX = viewportPos.x + style.safeMargin;
                const float minY = viewportPos.y + style.safeMargin;
                const float maxX = viewportPos.x + viewportSize.x - style.safeMargin - widgetSize.x;
                const float maxY = viewportPos.y + viewportSize.y - style.safeMargin - widgetSize.y;
                widgetMin.x = std::clamp(widgetMin.x, minX, std::max(minX, maxX));
                widgetMin.y = std::clamp(widgetMin.y, minY, std::max(minY, maxY));
            }

            const ImVec2 widgetMax(widgetMin.x + widgetSize.x, widgetMin.y + widgetSize.y);

            static float smoothedSpeedKmh = 0.0f;
            const float rawSpeedKmh = std::abs(state.bikeSpeed) * style.speedMultiplier;
            if (style.useSmoothing) {
                smoothedSpeedKmh += (rawSpeedKmh - smoothedSpeedKmh) * style.smoothingFactor;
            }
            else {
                smoothedSpeedKmh = rawSpeedKmh;
            }

            float pulseScale = 1.0f;
            if (style.pulseWhenFast && smoothedSpeedKmh >= style.pulseStartSpeed) {
                pulseScale += std::sin(static_cast<float>(ImGui::GetTime()) * style.pulseSpeed) * style.pulseAmplitude;
            }

            const ImVec2 scaledWidgetSize(widgetSize.x * pulseScale, widgetSize.y * pulseScale);
            const ImVec2 scaledWidgetCenter(widgetMin.x + widgetSize.x * 0.5f, widgetMin.y + widgetSize.y * 0.5f);
            const ImVec2 scaledWidgetMin(
                scaledWidgetCenter.x - scaledWidgetSize.x * 0.5f,
                scaledWidgetCenter.y - scaledWidgetSize.y * 0.5f);
            const ImVec2 scaledWidgetMax(
                scaledWidgetMin.x + scaledWidgetSize.x,
                scaledWidgetMin.y + scaledWidgetSize.y);

            ImGui::SetNextWindowPos(viewportPos, ImGuiCond_Always);
            ImGui::SetNextWindowSize(viewportSize, ImGuiCond_Always);
            ImGui::SetNextWindowBgAlpha(0.0f);

            ImGuiWindowFlags overlayFlags = ImGuiWindowFlags_NoDecoration
                | ImGuiWindowFlags_NoDocking
                | ImGuiWindowFlags_NoSavedSettings
                | ImGuiWindowFlags_NoFocusOnAppearing
                | ImGuiWindowFlags_NoInputs
                | ImGuiWindowFlags_NoNav
                | ImGuiWindowFlags_NoMove;

            if (!ImGui::Begin("Game HUD Overlay###SpeedometerDemo", nullptr, overlayFlags)) {
                ImGui::End();
                return;
            }

            ImDrawList* drawList = ImGui::GetWindowDrawList();
            if (style.clipToViewport) {
                drawList->PushClipRect(viewportPos, ImVec2(viewportPos.x + viewportSize.x, viewportPos.y + viewportSize.y), true);
            }

            VkDescriptorSet speedometerTexture = VK_NULL_HANDLE;
            if (style.useBackgroundImage && style.backgroundTexturePath && style.backgroundTexturePath[0] != '\0') {
                speedometerTexture = renderSys->GetContentBrowserThumbnail(style.backgroundTexturePath);
            }

            if (speedometerTexture != VK_NULL_HANDLE) {
                drawList->AddImage(
                    (ImTextureID)speedometerTexture,
                    scaledWidgetMin,
                    scaledWidgetMax,
                    ImVec2(0.0f, 0.0f),
                    ImVec2(1.0f, 1.0f),
                    ToU32(style.backgroundTint));
            }
            else {
                drawList->AddRectFilled(scaledWidgetMin, scaledWidgetMax, ToU32(style.fallbackPanelColor), style.fallbackPanelRounding);
            }

            if (style.drawDebugBounds) {
                drawList->AddRect(scaledWidgetMin, scaledWidgetMax, ToU32(style.debugBoundsColor), style.fallbackPanelRounding, 0, 2.0f);
            }

            ImFont* baseFont = ImGui::GetFont();
            const float titleFontSize = style.titleFontSize * pulseScale;
            const float numberFontSize = style.numberFontSize * style.numberFontScale * pulseScale;
            const float unitFontSize = style.unitFontSize * style.unitFontScale * pulseScale;

            char speedText[32] = {};
            if (style.padWithZeros) {
                std::snprintf(speedText, sizeof(speedText), "%0*.*f", style.minimumIntegerDigits, style.decimals, smoothedSpeedKmh);
            }
            else {
                std::snprintf(speedText, sizeof(speedText), "%.*f", style.decimals, smoothedSpeedKmh);
            }

            if (style.showTitle) {
                const ImVec2 titleAnchor(
                    scaledWidgetMin.x + scaledWidgetSize.x * 0.5f + style.titleOffset.x,
                    scaledWidgetMin.y + style.titleOffset.y * pulseScale);
                const ImVec2 titlePos = CalculateTextPosition(baseFont, titleFontSize, style.titleText, titleAnchor, style.centerTitle);
                DrawStyledText(drawList, baseFont, titleFontSize, titlePos, ToU32(style.titleColor), style.titleText, style);
            }

            {
                const ImVec2 numberAnchor(
                    scaledWidgetMin.x + scaledWidgetSize.x * 0.5f + style.numberOffset.x,
                    scaledWidgetMin.y + style.numberOffset.y * pulseScale);
                const ImVec2 numberPos = CalculateTextPosition(baseFont, numberFontSize, speedText, numberAnchor, style.centerNumber);
                DrawStyledText(drawList, baseFont, numberFontSize, numberPos, ToU32(style.numberColor), speedText, style);
            }

            if (style.showUnitLabel) {
                const ImVec2 unitAnchor(
                    scaledWidgetMin.x + scaledWidgetSize.x * 0.5f + style.unitOffset.x,
                    scaledWidgetMin.y + style.unitOffset.y * pulseScale);
                const ImVec2 unitPos = CalculateTextPosition(baseFont, unitFontSize, style.unitLabel, unitAnchor, style.centerUnit);
                DrawStyledText(drawList, baseFont, unitFontSize, unitPos, ToU32(style.unitColor), style.unitLabel, style);
            }

            // Extra teaching examples you can reuse later:
            // drawList->AddCircleFilled(center, 8.0f, IM_COL32_WHITE); // Draw a marker / indicator light.
            // drawList->AddRect(widgetMin, widgetMax, IM_COL32(255, 0, 0, 255)); // Manual border.
            // drawList->AddImage(iconTex, iconMin, iconMax); // Add another image on top of the speedometer.
            // drawList->AddText(baseFont, 18.0f, pos, IM_COL32_WHITE, "NITRO"); // Add a mode label.
            // if (state.isExtremeSpeed) { ... } // Trigger special UI state at high speed.

            if (style.clipToViewport) {
                drawList->PopClipRect();
            }

            ImGui::End();
        }

        void DrawViewportHint(const UserState& state, const ImVec2& viewportPos, const ImVec2& viewportSize) {
            if (viewportSize.x <= 1.0f || viewportSize.y <= 1.0f) {
                return;
            }

            const char* engineUiLabel = state.showEngineUi ? "F1 Hide Engine UI" : "F1 Show Engine UI";

            ImGui::SetNextWindowBgAlpha(0.0f);
            ImGui::SetNextWindowPos(viewportPos, ImGuiCond_Always);
            ImGui::SetNextWindowSize(viewportSize, ImGuiCond_Always);

            ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration
                | ImGuiWindowFlags_NoDocking
                | ImGuiWindowFlags_NoSavedSettings
                | ImGuiWindowFlags_NoFocusOnAppearing
                | ImGuiWindowFlags_NoInputs
                | ImGuiWindowFlags_NoNav
                | ImGuiWindowFlags_NoMove;

            if (!ImGui::Begin("Game HUD Hint Overlay###SpeedometerDemo", nullptr, flags)) {
                ImGui::End();
                return;
            }

            ImDrawList* drawList = ImGui::GetWindowDrawList();
            ImFont* font = ImGui::GetFont();
            const char* hintText = "WASD Move  RMB Look";
            const float fontSize = 18.0f;

            ImVec2 engineTextSize = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, engineUiLabel);
            ImVec2 hintTextSize = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, hintText);

            ImVec2 engineTextPos(
                viewportPos.x + viewportSize.x - 24.0f - engineTextSize.x,
                viewportPos.y + viewportSize.y - 52.0f);
            ImVec2 hintPos(
                viewportPos.x + viewportSize.x - 24.0f - hintTextSize.x,
                viewportPos.y + viewportSize.y - 28.0f);

            drawList->AddText(font, fontSize, ImVec2(engineTextPos.x + 1.0f, engineTextPos.y + 1.0f), IM_COL32(0, 0, 0, 180), engineUiLabel);
            drawList->AddText(font, fontSize, engineTextPos, IM_COL32(255, 255, 255, 235), engineUiLabel);

            drawList->AddText(font, fontSize, ImVec2(hintPos.x + 1.0f, hintPos.y + 1.0f), IM_COL32(0, 0, 0, 180), hintText);
            drawList->AddText(font, fontSize, hintPos, IM_COL32(255, 255, 255, 235), hintText);

            ImGui::End();
        }
    } // namespace

    void GameUi::DrawHud(RenderSystem* renderSys, const UserState& state, const ImVec2& viewportPos, const ImVec2& viewportSize) {
        DrawImageSpeedometerDemo(renderSys, state, viewportPos, viewportSize);
        DrawViewportHint(state, viewportPos, viewportSize);
    }
} // namespace engine
