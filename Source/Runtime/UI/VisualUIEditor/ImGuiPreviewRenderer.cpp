
//  UI 渲染后端
// 一切绘制最终都落在 ImDrawList 上，只在编辑器或调试场景下能用


#include "ImGuiPreviewRenderer.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cfloat>
#include <cmath>
#include <filesystem>
#include <format>
#include <string>
#include <unordered_map>
#include <vector>

#include <imgui.h>

#include "UITheme.hpp"

namespace engine {

    namespace {

        // 这里是一套完全基于 ImGui DrawList 的预览绘制辅助函数。
        // 它既服务于 Game UI Editor，也临时承担运行时 UI 的可视化输出。
        // 计算元素在父级坐标空间中的最终矩形；Canvas 直接继承父矩形。
        UIRect ComputeElementRect(const UIElement& element, const UIRect& parentRect) {
            if (element.GetType() == UIElementType::Canvas) {
                return parentRect;
            }

            return element.transform.ComputeRect(parentRect);
        }

        // 将 UI 逻辑坐标转换为 ImGui 屏幕像素坐标（考虑偏移与缩放）。
        ImVec2 ToImGuiPoint(const glm::vec2& point, const UIRenderContext& context) {
            return ImVec2(
                context.rootPosition.x + point.x * context.axisScale.x,
                context.rootPosition.y + point.y * context.axisScale.y);
        }

        // 通过解析回调将资源路径转换为 ImTextureID；路径为空或无解析器时返回空。
        ImTextureID ResolvePreviewTexture(
            const std::function<void*(const std::string&)>& resolver,
            const std::string& assetPath) {
            if (!resolver || assetPath.empty()) {
                return static_cast<ImTextureID>(0);
            }

            return reinterpret_cast<ImTextureID>(resolver(assetPath));
        }

        // 将 glm::vec4 颜色（含透明度因子）转换为 ImGui 32 位颜色值。
        ImU32 ToImGuiColor(const glm::vec4& color, float opacity = 1.0f) {
            return IM_COL32(
                static_cast<int>(std::clamp(color.r, 0.0f, 1.0f) * 255.0f),
                static_cast<int>(std::clamp(color.g, 0.0f, 1.0f) * 255.0f),
                static_cast<int>(std::clamp(color.b, 0.0f, 1.0f) * 255.0f),
                static_cast<int>(std::clamp(color.a * opacity, 0.0f, 1.0f) * 255.0f));
        }

        // 计算样式透明度与上下文透明度叠加后的最终不透明度。
        float ComputeEffectiveOpacity(const UIStyle& style, const UIRenderContext& context) {
            return std::clamp(style.opacity * context.opacityMultiplier, 0.0f, 1.0f);
        }

        // 根据上下文缩放计算字体大小；SpeedText 元素额外应用速度文本缩放。
        float ComputeScaledFontSize(const UIElement& element, const UIStyle& style, const UIRenderContext& context) {
            float fontSize = std::max(8.0f, style.fontSize * context.scale);
            if (element.GetName() == "SpeedText") {
                fontSize *= std::max(0.1f, context.speedTextScale);
            }
            return fontSize;
        }

        // 将相对/简写资源路径规范化为可访问的文件系统路径。
        std::filesystem::path ResolveAssetPath(const std::string& rawPath) {
            if (rawPath.empty()) {
                return {};
            }

            std::filesystem::path path(rawPath);
            if (path.is_absolute()) {
                return path;
            }

            const std::string normalized = path.generic_string();
            if (normalized.starts_with("assets/")) {
                return std::filesystem::path("Assets") / normalized.substr(7);
            }
            if (normalized.starts_with("Assets/")) {
                return path;
            }

            return path;
        }

        // 根据上下文缩放计算边框宽度，最小为 1 像素。
        float ComputeScaledBorderWidth(const UIStyle& style, const UIRenderContext& context) {
            if (style.borderWidth <= 0.0f) {
                return 0.0f;
            }

            return std::max(1.0f, style.borderWidth * context.scale);
        }

        // 根据上下文缩放计算圆角半径。
        float ComputeScaledBorderRadius(const UIStyle& style, const UIRenderContext& context) {
            if (style.borderRadius <= 0.0f) {
                return 0.0f;
            }

            return std::max(0.0f, style.borderRadius * context.scale);
        }

        ImFont* ResolvePreviewFont(const std::string& fontPath) {
            // 预览字体按路径缓存，避免每帧重复往 ImGui 字体图集里堆字体。
            if (fontPath.empty()) {
                return nullptr;
            }

            static std::unordered_map<std::string, ImFont*> fontCache;
            const std::filesystem::path resolvedPath = ResolveAssetPath(fontPath);
            const std::string cacheKey = resolvedPath.generic_string();
            if (const auto iterator = fontCache.find(cacheKey); iterator != fontCache.end()) {
                return iterator->second;
            }

            if (resolvedPath.empty() || !std::filesystem::exists(resolvedPath)) {
                fontCache[cacheKey] = nullptr;
                return nullptr;
            }

            ImGuiIO& io = ImGui::GetIO();
            ImFont* font = io.Fonts->AddFontFromFileTTF(
                cacheKey.c_str(),
                18.0f,
                nullptr,
                io.Fonts->GetGlyphRangesChineseFull());
            if (font) {
                io.Fonts->Build();
            }

            fontCache[cacheKey] = font;
            return font;
        }

        // 将一个点绕指定中心旋转给定弧度角。
        ImVec2 RotatePoint(const ImVec2& point, const ImVec2& pivot, float rotationRadians) {
            const float cosine = std::cos(rotationRadians);
            const float sine = std::sin(rotationRadians);
            const ImVec2 translated(point.x - pivot.x, point.y - pivot.y);
            return ImVec2(
                pivot.x + translated.x * cosine - translated.y * sine,
                pivot.y + translated.x * sine + translated.y * cosine);
        }

        // 根据元素的 pivot 和旋转角度构建旋转后的四边形顶点。
        std::array<ImVec2, 4> BuildRotatedQuad(const UIElement& element, const ImVec2& min, const ImVec2& max) {
            const ImVec2 pivot(
                min.x + (max.x - min.x) * element.transform.pivot.x,
                min.y + (max.y - min.y) * element.transform.pivot.y);
            const float rotationRadians = glm::radians(element.transform.rotation);
            return {
                RotatePoint(min, pivot, rotationRadians),
                RotatePoint(ImVec2(max.x, min.y), pivot, rotationRadians),
                RotatePoint(max, pivot, rotationRadians),
                RotatePoint(ImVec2(min.x, max.y), pivot, rotationRadians)
            };
        }

        // 判断元素是否存在可见的旋转（忽略极小角度）。
        bool HasVisibleRotation(const UIElement& element) {
            return std::abs(element.transform.rotation) > 0.01f;
        }

        // 按 zOrder 排序子元素，保证绘制顺序正确（稳定排序保持同层级顺序）。
        std::vector<const UIElement*> GetChildrenSortedForDraw(const UIElement& element) {
            std::vector<const UIElement*> children;
            children.reserve(element.GetChildren().size());
            for (const auto& child : element.GetChildren()) {
                children.push_back(child.get());
            }

            std::stable_sort(children.begin(), children.end(), [](const UIElement* lhs, const UIElement* rhs) {
                return lhs->zOrder < rhs->zOrder;
            });
            return children;
        }

        // 绘制元素边框：支持旋转四边形和带圆角的矩形两种模式。
        void DrawPreviewBorder(
            ImDrawList* drawList,
            const UIElement& element,
            const UIStyle& style,
            const ImVec2& min,
            const ImVec2& max,
            const UIRenderContext& context,
            ImU32 color) {
            const float scaledBorderWidth = ComputeScaledBorderWidth(style, context);
            if (scaledBorderWidth <= 0.0f) {
                return;
            }

            const float scaledBorderRadius = ComputeScaledBorderRadius(style, context);
            if (HasVisibleRotation(element)) {
                const auto quad = BuildRotatedQuad(element, min, max);
                drawList->AddQuad(quad[0], quad[1], quad[2], quad[3], color, scaledBorderWidth);
            }
            else {
                drawList->AddRect(min, max, color, scaledBorderRadius, 0, scaledBorderWidth);
            }
        }

        // 尝试绘制元素的贴图；成功返回 true，无有效贴图则返回 false 供调用方回退。
        bool DrawPreviewTexture(
            ImDrawList* drawList,
            const UIElement& element,
            const UIStyle& style,
            const ImVec2& min,
            const ImVec2& max,
            const UIRenderContext& context,
            const std::function<void*(const std::string&)>& resolver,
            const std::string& texturePath,
            bool useTint) {
            const ImTextureID textureId = ResolvePreviewTexture(resolver, texturePath);
            if (textureId == static_cast<ImTextureID>(0)) {
                return false;
            }

            const float effectiveOpacity = ComputeEffectiveOpacity(style, context);
            const ImU32 tintColor = useTint
                ? ToImGuiColor(style.tintColor, effectiveOpacity)
                : IM_COL32(255, 255, 255, static_cast<int>(effectiveOpacity * 255.0f));

            // 旋转元素使用四点图片绘制，未旋转元素走普通矩形贴图路径。
            if (HasVisibleRotation(element)) {
                const auto quad = BuildRotatedQuad(element, min, max);
                drawList->AddImageQuad(
                    textureId,
                    quad[0],
                    quad[1],
                    quad[2],
                    quad[3],
                    ImVec2(0.0f, 1.0f),
                    ImVec2(1.0f, 1.0f),
                    ImVec2(1.0f, 0.0f),
                    ImVec2(0.0f, 0.0f),
                    tintColor);
            }
            else {
                drawList->AddImage(textureId, min, max, ImVec2(0.0f, 1.0f), ImVec2(1.0f, 0.0f), tintColor);
            }

            DrawPreviewBorder(drawList, element, style, min, max, context, ToImGuiColor(style.borderColor, effectiveOpacity));
            return true;
        }

        float NormalizeWidgetValue(float value, float minValue, float maxValue);

        void DrawText(
            ImDrawList* drawList,
            const std::string& text,
            const ImVec2& min,
            const ImVec2& max,
            ImU32 color,
            float fontSizePixels,
            const std::string& alignment,
            ImFont* font,
            bool wrapText);

        float DegreesToRadians(float degrees) {
            return degrees * 0.01745329251994329577f;
        }

        ImVec2 RotateAroundPoint(const ImVec2& point, const ImVec2& center, float radians) {
            const float sine = std::sin(radians);
            const float cosine = std::cos(radians);
            const float dx = point.x - center.x;
            const float dy = point.y - center.y;
            return ImVec2(
                center.x + dx * cosine - dy * sine,
                center.y + dx * sine + dy * cosine);
        }

        ImVec2 MakeRadialPoint(const ImVec2& center, float radius, float angleRadians) {
            return ImVec2(
                center.x + std::cos(angleRadians) * radius,
                center.y + std::sin(angleRadians) * radius);
        }

        ImVec2 MakeRadialUv(const ImVec2& point, const ImVec2& min, const ImVec2& max) {
            const float width = std::max(1.0f, max.x - min.x);
            const float height = std::max(1.0f, max.y - min.y);
            return ImVec2(
                std::clamp((point.x - min.x) / width, 0.0f, 1.0f),
                std::clamp(1.0f - ((point.y - min.y) / height), 0.0f, 1.0f));
        }

        void DrawSolidRadialArc(
            ImDrawList* drawList,
            const ImVec2& center,
            float innerRadius,
            float outerRadius,
            float startAngle,
            float endAngle,
            ImU32 color) {
            const float thickness = std::max(1.0f, outerRadius - innerRadius);
            const float centerRadius = innerRadius + thickness * 0.5f;
            const int segmentCount = std::clamp(
                static_cast<int>(std::ceil(std::abs(endAngle - startAngle) * std::max(centerRadius, 1.0f) / 10.0f)),
                12,
                180);
            drawList->PathClear();
            for (int segmentIndex = 0; segmentIndex <= segmentCount; ++segmentIndex) {
                const float t = static_cast<float>(segmentIndex) / static_cast<float>(segmentCount);
                const float angle = startAngle + (endAngle - startAngle) * t;
                drawList->PathLineTo(MakeRadialPoint(center, centerRadius, angle));
            }
            drawList->PathStroke(color, false, thickness);
        }

        bool DrawTexturedRadialArc(
            ImDrawList* drawList,
            const UIElement& element,
            const ImVec2& min,
            const ImVec2& max,
            const std::string& texturePath,
            float innerRadius,
            float outerRadius,
            float startAngle,
            float endAngle,
            ImU32 tintColor,
            const std::function<void* (const std::string&)>& textureResolver) {
            const ImTextureID textureId = ResolvePreviewTexture(textureResolver, texturePath);
            if (textureId == static_cast<ImTextureID>(0)) {
                return false;
            }

            const ImVec2 center((min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f);
            const float rotationRadians = DegreesToRadians(element.transform.rotation);
            const int segmentCount = std::clamp(
                static_cast<int>(std::ceil(std::abs(endAngle - startAngle) * std::max(outerRadius, 1.0f) / 10.0f)),
                12,
                180);
            const float step = (endAngle - startAngle) / static_cast<float>(segmentCount);

            for (int segmentIndex = 0; segmentIndex < segmentCount; ++segmentIndex) {
                const float angleA = startAngle + step * static_cast<float>(segmentIndex);
                const float angleB = startAngle + step * static_cast<float>(segmentIndex + 1);

                const ImVec2 outerA = MakeRadialPoint(center, outerRadius, angleA);
                const ImVec2 outerB = MakeRadialPoint(center, outerRadius, angleB);
                const ImVec2 innerA = MakeRadialPoint(center, innerRadius, angleA);
                const ImVec2 innerB = MakeRadialPoint(center, innerRadius, angleB);

                const ImVec2 uvOuterA = MakeRadialUv(outerA, min, max);
                const ImVec2 uvOuterB = MakeRadialUv(outerB, min, max);
                const ImVec2 uvInnerA = MakeRadialUv(innerA, min, max);
                const ImVec2 uvInnerB = MakeRadialUv(innerB, min, max);

                const ImVec2 drawOuterA = HasVisibleRotation(element) ? RotateAroundPoint(outerA, center, rotationRadians) : outerA;
                const ImVec2 drawOuterB = HasVisibleRotation(element) ? RotateAroundPoint(outerB, center, rotationRadians) : outerB;
                const ImVec2 drawInnerA = HasVisibleRotation(element) ? RotateAroundPoint(innerA, center, rotationRadians) : innerA;
                const ImVec2 drawInnerB = HasVisibleRotation(element) ? RotateAroundPoint(innerB, center, rotationRadians) : innerB;

                drawList->AddImageQuad(
                    textureId,
                    drawOuterA,
                    drawOuterB,
                    drawInnerB,
                    drawInnerA,
                    uvOuterA,
                    uvOuterB,
                    uvInnerB,
                    uvInnerA,
                    tintColor);
            }

            return true;
        }
		// 绘制环形进度条
        void DrawPreviewRadialProgressBar(
            ImDrawList* drawList,
            const UIElement& element,
            const UIStyle& style,
            const UIRadialProgressBar& radialProgressBar,
            const ImVec2& min,
            const ImVec2& max,
            const UIRenderContext& context,
            float fontSize,
            ImFont* font,
            ImU32 textColor,
            const std::function<void* (const std::string&)>& textureResolver) {
            const float effectiveOpacity = ComputeEffectiveOpacity(style, context);
            const float normalized = NormalizeWidgetValue(radialProgressBar.value, radialProgressBar.minValue, radialProgressBar.maxValue);
            const ImVec2 center((min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f);
            const float maxOuterRadius = std::max(2.0f, std::min(max.x - min.x, max.y - min.y) * 0.5f - 1.0f);
            const float outerRadius = maxOuterRadius * std::clamp(radialProgressBar.outerRadiusRatio, 0.05f, 1.0f);
            const float innerRadius = outerRadius * std::clamp(radialProgressBar.innerRadiusRatio, 0.05f, 0.98f);
            const float fullSweepRadians = DegreesToRadians(std::max(0.0f, radialProgressBar.sweepAngleDegrees));
            const float startAngle = DegreesToRadians(radialProgressBar.startAngleDegrees);
            const float direction = radialProgressBar.clockwise ? 1.0f : -1.0f;
            const float fullEndAngle = startAngle + direction * fullSweepRadians;
            const float fillEndAngle = startAngle + direction * fullSweepRadians * normalized;

            const ImU32 backgroundTint = radialProgressBar.tintBackgroundImage
                ? ToImGuiColor(radialProgressBar.backgroundFillColor, effectiveOpacity)
                : IM_COL32(255, 255, 255, static_cast<int>(std::clamp(effectiveOpacity, 0.0f, 1.0f) * 255.0f));
            if (!radialProgressBar.backgroundImagePath.empty()) {
                DrawTexturedRadialArc(
                    drawList,
                    element,
                    min,
                    max,
                    radialProgressBar.backgroundImagePath,
                    innerRadius,
                    outerRadius,
                    startAngle,
                    fullEndAngle,
                    backgroundTint,
                    textureResolver);
            }
            else {
                DrawSolidRadialArc(
                    drawList,
                    center,
                    innerRadius,
                    outerRadius,
                    startAngle,
                    fullEndAngle,
                    ToImGuiColor(radialProgressBar.backgroundFillColor, effectiveOpacity));
            }

            if (normalized > 0.0f) {
                if (!radialProgressBar.fillImagePath.empty()) {
                    const ImU32 fillTint = radialProgressBar.tintFillImage
                        ? ToImGuiColor(radialProgressBar.fillColor, effectiveOpacity)
                        : IM_COL32(255, 255, 255, static_cast<int>(std::clamp(effectiveOpacity, 0.0f, 1.0f) * 255.0f));
                    DrawTexturedRadialArc(
                        drawList,
                        element,
                        min,
                        max,
                        radialProgressBar.fillImagePath,
                        innerRadius,
                        outerRadius,
                        startAngle,
                        fillEndAngle,
                        fillTint,
                        textureResolver);
                }
                else {
                    DrawSolidRadialArc(
                        drawList,
                        center,
                        innerRadius,
                        outerRadius,
                        startAngle,
                        fillEndAngle,
                        ToImGuiColor(radialProgressBar.fillColor, effectiveOpacity));
                }
            }

            if (radialProgressBar.showPercentage) {
                const std::string percentage = std::format("{:.0f}%", normalized * 100.0f);
                DrawText(drawList, percentage, min, max, textColor, fontSize, "Center", font, false);
            }
        }


        float NormalizeWidgetValue(float value, float minValue, float maxValue) {
            const float range = maxValue - minValue;
            if (std::abs(range) <= 0.0001f) {
                return 0.0f;
            }

            return std::clamp((value - minValue) / range, 0.0f, 1.0f);
        }

        void DrawPreviewSlider(
            ImDrawList* drawList,
            const UIElement& element,
            const UIStyle& style,
            const UISlider& slider,
            const ImVec2& min,
            const ImVec2& max,
            const UIRenderContext& context) {
            const float effectiveOpacity = ComputeEffectiveOpacity(style, context);
            const float normalized = NormalizeWidgetValue(slider.value, slider.minValue, slider.maxValue);
            const float radius = std::max(6.0f, (max.y - min.y) * 0.5f);
            const float trackHeight = std::max(6.0f, (max.y - min.y) * 0.28f);
            const float trackY = min.y + (max.y - min.y - trackHeight) * 0.5f;
            const float handleCenterX = min.x + (max.x - min.x) * normalized;
            const float handleCenterY = (min.y + max.y) * 0.5f;

            drawList->AddRectFilled(
                ImVec2(min.x, trackY),
                ImVec2(max.x, trackY + trackHeight),
                ToImGuiColor(style.backgroundColor, effectiveOpacity),
                trackHeight * 0.5f);
            drawList->AddRectFilled(
                ImVec2(min.x, trackY),
                ImVec2(handleCenterX, trackY + trackHeight),
                ToImGuiColor(slider.fillColor, effectiveOpacity),
                trackHeight * 0.5f);
            drawList->AddCircleFilled(
                ImVec2(handleCenterX, handleCenterY),
                radius,
                ToImGuiColor(slider.handleColor, effectiveOpacity),
                24);
            DrawPreviewBorder(drawList, element, style, min, max, context, ToImGuiColor(style.borderColor, effectiveOpacity));
        }

        void DrawPreviewToggle(
            ImDrawList* drawList,
            const UIElement& element,
            const UIStyle& style,
            const UIToggle& toggle,
            const ImVec2& min,
            const ImVec2& max,
            const UIRenderContext& context,
            float fontSize,
            ImFont* font,
            ImU32 textColor) {
            const float effectiveOpacity = ComputeEffectiveOpacity(style, context);
            const float switchWidth = std::min(max.x - min.x, (max.y - min.y) * 1.8f);
            const ImVec2 switchMin(min.x, min.y + (max.y - min.y) * 0.15f);
            const ImVec2 switchMax(min.x + switchWidth, max.y - (max.y - min.y) * 0.15f);
            const float knobRadius = std::max(4.0f, (switchMax.y - switchMin.y) * 0.42f);
            const float knobCenterY = (switchMin.y + switchMax.y) * 0.5f;
            const float knobCenterX = toggle.isOn
                ? (switchMax.x - knobRadius - 3.0f)
                : (switchMin.x + knobRadius + 3.0f);

            drawList->AddRectFilled(
                switchMin,
                switchMax,
                ToImGuiColor(toggle.isOn ? toggle.onColor : toggle.offColor, effectiveOpacity),
                (switchMax.y - switchMin.y) * 0.5f);
            drawList->AddCircleFilled(
                ImVec2(knobCenterX, knobCenterY),
                knobRadius,
                ToImGuiColor(toggle.knobColor, effectiveOpacity),
                24);
            DrawPreviewBorder(drawList, element, style, min, max, context, ToImGuiColor(style.borderColor, effectiveOpacity));

            if (!toggle.label.empty()) {
                DrawText(drawList, toggle.label, ImVec2(switchMax.x + 8.0f, min.y), max, textColor, fontSize, "Left", font, false);
            }
        }

        void DrawPreviewProgressBar(
            ImDrawList* drawList,
            const UIElement& element,
            const UIStyle& style,
            const UIProgressBar& progressBar,
            const ImVec2& min,
            const ImVec2& max,
            const UIRenderContext& context,
            float fontSize,
            ImFont* font,
            ImU32 textColor) {
            const float effectiveOpacity = ComputeEffectiveOpacity(style, context);
            const float normalized = NormalizeWidgetValue(progressBar.value, progressBar.minValue, progressBar.maxValue);
            drawList->AddRectFilled(
                min,
                max,
                ToImGuiColor(style.backgroundColor, effectiveOpacity),
                std::max(2.0f, ComputeScaledBorderRadius(style, context)));
            drawList->AddRectFilled(
                min,
                ImVec2(min.x + (max.x - min.x) * normalized, max.y),
                ToImGuiColor(progressBar.fillColor, effectiveOpacity),
                std::max(2.0f, ComputeScaledBorderRadius(style, context)));
            DrawPreviewBorder(drawList, element, style, min, max, context, ToImGuiColor(style.borderColor, effectiveOpacity));

            if (progressBar.showPercentage) {
                const std::string percentage = std::format("{:.0f}%", normalized * 100.0f);
                DrawText(drawList, percentage, min, max, textColor, fontSize, "Center", font, false);
            }
        }

        void DrawPreviewInputField(
            ImDrawList* drawList,
            const UIElement& element,
            const UIStyle& style,
            const UIInputField& inputField,
            const ImVec2& min,
            const ImVec2& max,
            const UIRenderContext& context,
            float fontSize,
            ImFont* font,
            ImU32 textColor) {
            const float effectiveOpacity = ComputeEffectiveOpacity(style, context);
            drawList->AddRectFilled(
                min,
                max,
                ToImGuiColor(style.backgroundColor, effectiveOpacity),
                ComputeScaledBorderRadius(style, context));
            DrawPreviewBorder(drawList, element, style, min, max, context, ToImGuiColor(style.borderColor, effectiveOpacity));

            std::string displayText = inputField.text;
            ImU32 displayColor = textColor;
            if (displayText.empty()) {
                displayText = inputField.placeholder.empty() ? std::string("Input") : inputField.placeholder;
                displayColor = IM_COL32(180, 184, 196, 190);
            }
            else if (inputField.password) {
                displayText.assign(inputField.text.size(), '*');
            }

            DrawText(drawList, displayText, min, max, displayColor, fontSize, "Left", font, false);
        }

        ImVec2 MeasureText(const std::string& text, ImFont* font, float fontSizePixels, float wrapWidth = 0.0f) {
            if (!font) {
                font = ImGui::GetFont();
            }
            if (!font) {
                return ImGui::CalcTextSize(text.c_str());
            }

            const float maxWidth = wrapWidth > 0.0f ? wrapWidth : FLT_MAX;
            return font->CalcTextSizeA(fontSizePixels, maxWidth, wrapWidth, text.c_str());
        }

        ImVec2 ComputeAlignedTextPosition(
            const std::string& alignment,
            const ImVec2& min,
            const ImVec2& max,
            const ImVec2& textSize,
            float padding = 8.0f) {
            std::string alignmentLower = alignment;
            std::transform(alignmentLower.begin(), alignmentLower.end(), alignmentLower.begin(), [](unsigned char character) {
                return static_cast<char>(std::tolower(character));
            });

            float x = min.x + padding;
            if (alignmentLower == "center") {
                x = min.x + std::max(padding, (max.x - min.x - textSize.x) * 0.5f);
            }
            else if (alignmentLower == "right") {
                x = std::max(min.x + padding, max.x - textSize.x - padding);
            }

            const float y = std::clamp(
                min.y + padding,
                min.y + 2.0f,
                std::max(min.y + 2.0f, max.y - textSize.y - 2.0f));

            return ImVec2(x, y);
        }

        void DrawText(
            ImDrawList* drawList,
            const std::string& text,
            const ImVec2& min,
            const ImVec2& max,
            ImU32 color,
            float fontSizePixels,
            const std::string& alignment,
            ImFont* font,
            bool wrapText) {
            const float effectiveFontSize = fontSizePixels > 0.0f ? fontSizePixels : ImGui::GetFontSize();
            const float wrapWidth = wrapText ? std::max(0.0f, (max.x - min.x) - 16.0f) : 0.0f;
            const ImVec2 textSize = MeasureText(text, font, effectiveFontSize, wrapWidth);
            const ImVec2 textPos = ComputeAlignedTextPosition(alignment, min, max, textSize);
            drawList->AddText(font, effectiveFontSize, textPos, color, text.c_str(), nullptr, wrapWidth);
        }

        void RenderElement(
            const UIElement& element,
            const UIRect& parentRect,
            ImDrawList* drawList,
            const UIRenderContext& context,
            const UITheme* theme,
            const std::function<void*(const std::string&)>& textureResolver,
            UIElementId hoveredElementId,
            UIElementId pressedElementId) {
            // RenderElement 直接递归消费运行时 UI 树，不维护额外的镜像数据结构。
            if (element.GetType() != UIElementType::Canvas && !element.visible) {
                return;
            }

            const UIRect elementRect = ComputeElementRect(element, parentRect);
            const UIStyle resolvedStyle = ResolveStyle(element.style, theme);
            const ImVec2 min = ToImGuiPoint(elementRect.position, context);
            const ImVec2 max = ToImGuiPoint(elementRect.position + elementRect.size, context);
            const float effectiveOpacity = ComputeEffectiveOpacity(resolvedStyle, context);
            const ImU32 borderColor = ToImGuiColor(resolvedStyle.borderColor, effectiveOpacity);
            const ImU32 fillColor = ToImGuiColor(resolvedStyle.backgroundColor, effectiveOpacity);
            const ImU32 textColor = ToImGuiColor(resolvedStyle.textColor, effectiveOpacity);
            const float scaledBorderRadius = ComputeScaledBorderRadius(resolvedStyle, context);
            const float scaledFontSize = ComputeScaledFontSize(element, resolvedStyle, context);
            ImFont* previewFont = ResolvePreviewFont(resolvedStyle.fontPath);

            switch (element.GetType()) {
            case UIElementType::Canvas:
                break;
            case UIElementType::Panel:
                if (!DrawPreviewTexture(drawList, element, resolvedStyle, min, max, context, textureResolver, resolvedStyle.texturePath, true)) {
                    if (HasVisibleRotation(element)) {
                        const auto quad = BuildRotatedQuad(element, min, max);
                        drawList->AddQuadFilled(quad[0], quad[1], quad[2], quad[3], fillColor);
                    }
                    else {
                        drawList->AddRectFilled(min, max, fillColor, scaledBorderRadius);
                    }
                    DrawPreviewBorder(drawList, element, resolvedStyle, min, max, context, borderColor);
                }
                break;
            case UIElementType::Image: {
                const auto* image = dynamic_cast<const UIImage*>(&element);
                const std::string& texturePath = image && !image->imagePath.empty()
                    ? image->imagePath
                    : resolvedStyle.texturePath;
                if (DrawPreviewTexture(drawList, element, resolvedStyle, min, max, context, textureResolver, texturePath, true)) {
                    break;
                }

                if (HasVisibleRotation(element)) {
                    const auto quad = BuildRotatedQuad(element, min, max);
                    drawList->AddQuadFilled(quad[0], quad[1], quad[2], quad[3], fillColor);
                    drawList->AddQuad(quad[0], quad[1], quad[2], quad[3], IM_COL32(210, 210, 220, 180), 1.0f);
                }
                else {
                    drawList->AddRectFilled(min, max, fillColor, scaledBorderRadius);
                    drawList->AddRect(min, max, IM_COL32(210, 210, 220, 180), scaledBorderRadius, 0, 1.0f);
                }
                std::string placeholder = "Image";
                if (image && !image->imagePath.empty()) {
                    placeholder = std::format("Image\n{}", image->imagePath);
                }
                drawList->AddLine(min, max, IM_COL32(180, 180, 190, 150), 1.0f);
                drawList->AddLine(ImVec2(min.x, max.y), ImVec2(max.x, min.y), IM_COL32(180, 180, 190, 150), 1.0f);
                DrawText(drawList, placeholder, min, max, textColor, scaledFontSize, "Left", previewFont, true);
                break;
            }
            case UIElementType::Text: {
                const auto* text = dynamic_cast<const UIText*>(&element);
                const std::string previewText = text ? text->text : element.GetName();
                DrawText(
                    drawList,
                    previewText.empty() ? std::string("Text") : previewText,
                    min,
                    max,
                    textColor,
                    scaledFontSize,
                    text ? text->alignment : std::string("Left"),
                    previewFont,
                    text ? text->wrapText : false);
                break;
            }
            case UIElementType::Button: {
                const auto* button = dynamic_cast<const UIButton*>(&element);
                const ResolvedUIButtonStyle buttonStyle = button
                    ? ResolveButtonStyle(element, *button, theme)
                    : ResolvedUIButtonStyle{};
                ImU32 buttonColor = fillColor;
                ImVec2 buttonMin = min;
                ImVec2 buttonMax = max;
                if (button) {
                    const glm::vec4* sourceColor = &buttonStyle.normalColor;
                    float visualScale = buttonStyle.normalScale;
                    if (button->runtimeVisualInitialized) {
                        sourceColor = &button->runtimeVisualColor;
                        visualScale = button->runtimeVisualScale;
                    }
                    buttonColor = ToImGuiColor(*sourceColor, effectiveOpacity);

                    if (std::abs(visualScale - 1.0f) > 0.0001f) {
                        const ImVec2 center((min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f);
                        const ImVec2 halfSize((max.x - min.x) * 0.5f * visualScale, (max.y - min.y) * 0.5f * visualScale);
                        buttonMin = ImVec2(center.x - halfSize.x, center.y - halfSize.y);
                        buttonMax = ImVec2(center.x + halfSize.x, center.y + halfSize.y);
                    }
                }

                const bool drewTexture =
                    !resolvedStyle.texturePath.empty() &&
                    DrawPreviewTexture(drawList, element, resolvedStyle, buttonMin, buttonMax, context, textureResolver, resolvedStyle.texturePath, false);
                if (!drewTexture) {
                    if (HasVisibleRotation(element)) {
                        const auto quad = BuildRotatedQuad(element, buttonMin, buttonMax);
                        drawList->AddQuadFilled(quad[0], quad[1], quad[2], quad[3], buttonColor);
                    }
                    else {
                        drawList->AddRectFilled(buttonMin, buttonMax, buttonColor, scaledBorderRadius);
                    }
                }
                else if (buttonColor != IM_COL32(255, 255, 255, static_cast<int>(effectiveOpacity * 255.0f))) {
                    const ImU32 overlayColor = IM_COL32(
                        (buttonColor >> IM_COL32_R_SHIFT) & 0xFF,
                        (buttonColor >> IM_COL32_G_SHIFT) & 0xFF,
                        (buttonColor >> IM_COL32_B_SHIFT) & 0xFF,
                        96);
                    if (HasVisibleRotation(element)) {
                        const auto quad = BuildRotatedQuad(element, buttonMin, buttonMax);
                        drawList->AddQuadFilled(quad[0], quad[1], quad[2], quad[3], overlayColor);
                    }
                    else {
                        drawList->AddRectFilled(buttonMin, buttonMax, overlayColor, scaledBorderRadius);
                    }
                }
                DrawPreviewBorder(drawList, element, resolvedStyle, buttonMin, buttonMax, context, borderColor);

                const std::string label = button && !button->label.empty() ? button->label : std::string("Button");
                const ImVec2 textSize = MeasureText(label, previewFont, scaledFontSize);
                const ImVec2 textPos(
                    buttonMin.x + std::max(8.0f, (buttonMax.x - buttonMin.x - textSize.x) * 0.5f),
                    buttonMin.y + std::max(6.0f, (buttonMax.y - buttonMin.y - textSize.y) * 0.5f));
                drawList->AddText(previewFont, scaledFontSize, textPos, textColor, label.c_str());
                break;
            }
            case UIElementType::Slider:
                if (const auto* slider = dynamic_cast<const UISlider*>(&element)) {
                    DrawPreviewSlider(drawList, element, resolvedStyle, *slider, min, max, context);
                }
                else {
                    drawList->AddRectFilled(min, max, fillColor, scaledBorderRadius);
                    DrawPreviewBorder(drawList, element, resolvedStyle, min, max, context, borderColor);
                }
                break;
            case UIElementType::Toggle:
                if (const auto* toggle = dynamic_cast<const UIToggle*>(&element)) {
                    DrawPreviewToggle(drawList, element, resolvedStyle, *toggle, min, max, context, scaledFontSize, previewFont, textColor);
                }
                else {
                    drawList->AddRectFilled(min, max, fillColor, scaledBorderRadius);
                    DrawPreviewBorder(drawList, element, resolvedStyle, min, max, context, borderColor);
                }
                break;
            case UIElementType::ProgressBar:
                if (const auto* progressBar = dynamic_cast<const UIProgressBar*>(&element)) {
                    DrawPreviewProgressBar(drawList, element, resolvedStyle, *progressBar, min, max, context, scaledFontSize, previewFont, textColor);
                }
                else {
                    drawList->AddRectFilled(min, max, fillColor, scaledBorderRadius);
                    DrawPreviewBorder(drawList, element, resolvedStyle, min, max, context, borderColor);
                }
                break;
            case UIElementType::RadialProgressBar:
                if (const auto* radialProgressBar = dynamic_cast<const UIRadialProgressBar*>(&element)) {
                    DrawPreviewRadialProgressBar(drawList, element, resolvedStyle, *radialProgressBar, min, max, context, scaledFontSize, previewFont, textColor, textureResolver);
                }
                else {
                    drawList->AddRectFilled(min, max, fillColor, scaledBorderRadius);
                    DrawPreviewBorder(drawList, element, resolvedStyle, min, max, context, borderColor);
                }
                break;
            case UIElementType::InputField:
                if (const auto* inputField = dynamic_cast<const UIInputField*>(&element)) {
                    DrawPreviewInputField(drawList, element, resolvedStyle, *inputField, min, max, context, scaledFontSize, previewFont, textColor);
                }
                else {
                    drawList->AddRectFilled(min, max, fillColor, scaledBorderRadius);
                    DrawPreviewBorder(drawList, element, resolvedStyle, min, max, context, borderColor);
                }
                break;
            default:
                if (HasVisibleRotation(element)) {
                    const auto quad = BuildRotatedQuad(element, min, max);
                    drawList->AddQuadFilled(quad[0], quad[1], quad[2], quad[3], fillColor);
                }
                else {
                    drawList->AddRectFilled(min, max, fillColor, scaledBorderRadius);
                }
                DrawPreviewBorder(drawList, element, resolvedStyle, min, max, context, borderColor);
                DrawText(drawList, std::string(ToString(element.GetType())), min, max, textColor, scaledFontSize, "Left", previewFont, false);
                break;
            }

            if (context.selectedElementId != 0 && context.selectedElementId == element.GetId()) {
                // 编辑器模式下用描边高亮当前选中元素。
                if (HasVisibleRotation(element)) {
                    const auto quad = BuildRotatedQuad(element, min, max);
                    drawList->AddQuad(quad[0], quad[1], quad[2], quad[3], IM_COL32(255, 204, 96, 255), 2.5f);
                }
                else {
                    drawList->AddRect(min, max, IM_COL32(255, 204, 96, 255), std::max(2.0f, scaledBorderRadius), 0, 2.5f);
                }
            }

            UIRenderContext childContext = context;
            childContext.opacityMultiplier = effectiveOpacity;
            for (const UIElement* child : GetChildrenSortedForDraw(element)) {
                RenderElement(*child, elementRect, drawList, childContext, theme, textureResolver, hoveredElementId, pressedElementId);
            }
        }

    } // namespace

    void ImGuiPreviewRenderer::SetTextureResolver(std::function<void*(const std::string&)> resolver) {
        // 外部把“资源路径 -> ImTextureID/VkDescriptorSet”的解析方式注入进来。
        mTextureResolver = std::move(resolver);
    }

    void ImGuiPreviewRenderer::RenderScreen(
        const UIScreen& screen,
        const UIRenderContext& context,
        UIElementId hoveredElementId,
        UIElementId pressedElementId) {
        // RenderScreen 是整张 UIScreen 的入口，
        // 实际绘制仍然由 RenderElement 递归遍历真实 UI 树完成。
        if (!screen.GetRootCanvas() || context.nativeContext == nullptr) {
            return;
        }

        ImDrawList* drawList = static_cast<ImDrawList*>(context.nativeContext);
        const UIRect rootRect{
            glm::vec2(0.0f),
            screen.GetReferenceResolution(),
            0.0f
        };

        UIRenderContext screenContext = context;
        if (screen.GetName() == "HUD") {
            screenContext.opacityMultiplier *= std::max(0.0f, context.hudOpacityMultiplier);
        }

        const auto theme = LoadUiTheme(screen.GetThemePath());
        RenderElement(*screen.GetRootCanvas(), rootRect, drawList, screenContext, theme.get(), mTextureResolver, hoveredElementId, pressedElementId);
    }

} // namespace engine
