#include "GameUi.hpp"

#include "../../Runtime/Renderer/RenderSystem.hpp"
#include "../../Runtime/UserState/UserState.hpp"

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
            // anchor = UiAnchor::TopLeft;     alignment = ImVec2(0.0f, 0.0f); // 宸︿笂瑙掑榻?
            // anchor = UiAnchor::Center;      alignment = ImVec2(0.5f, 0.5f); // 姝ｄ腑闂村眳涓?
            // anchor = UiAnchor::BottomRight; alignment = ImVec2(1.0f, 1.0f); // 鍙充笅瑙掕创杈?

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

        struct HealthHudStyle {
            bool enabled = true;
            UiAnchor anchor = UiAnchor::TopLeft;
            ImVec2 alignment = ImVec2(0.0f, 0.0f);
            ImVec2 offset = ImVec2(26.0f, 22.0f);

            ImVec2 panelSize = ImVec2(220.0f, 88.0f);
            float panelRounding = 22.0f;
            ImVec4 panelColor = ImVec4(0.06f, 0.09f, 0.12f, 0.78f);
            ImVec4 panelBorderColor = ImVec4(1.0f, 0.33f, 0.37f, 0.80f);
            float panelBorderThickness = 2.0f;
            float safeMargin = 8.0f;
            bool clipToViewport = true;

            const char* titleText = "LIVES";
            float titleFontSize = 18.0f;
            ImVec2 titleOffset = ImVec2(18.0f, 12.0f);
            ImVec4 titleColor = ImVec4(0.98f, 0.98f, 1.0f, 0.95f);

            ImVec2 heartsOffset = ImVec2(18.0f, 38.0f);
            float heartSize = 18.0f;
            float heartSpacing = 18.0f;
            ImVec4 heartFillColor = ImVec4(0.94f, 0.18f, 0.24f, 0.98f);
            ImVec4 heartGlossColor = ImVec4(1.0f, 0.72f, 0.76f, 0.35f);
            ImVec4 heartEmptyColor = ImVec4(0.25f, 0.12f, 0.14f, 0.72f);
            ImVec4 heartStrokeColor = ImVec4(1.0f, 0.90f, 0.92f, 0.18f);

            float labelFontSize = 24.0f;
            ImVec2 labelOffset = ImVec2(176.0f, 40.0f);
            ImVec4 labelColor = ImVec4(1.0f, 0.98f, 0.98f, 1.0f);
            bool centerLabel = false;

            bool drawTextShadow = true;
            ImVec2 shadowOffset = ImVec2(1.0f, 1.0f);
            ImVec4 shadowColor = ImVec4(0.0f, 0.0f, 0.0f, 0.65f);
            bool drawTextOutline = false;
            float outlineThickness = 1.0f;
            ImVec4 outlineColor = ImVec4(0.0f, 0.0f, 0.0f, 0.8f);
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

        void DrawStyledText(ImDrawList* drawList, ImFont* font, float fontSize, const ImVec2& position, ImU32 color, const char* text, const HealthHudStyle& style) {
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
            // 鏁欏鐢ㄩ厤缃尯锛?
            // 浠ュ悗浣犺鏀归鏍硷紝浼樺厛鏀硅繖閲屻€?
            // ==========================================

            style.enabled = true;
            style.backgroundTexturePath = "Assets/Textures/UiSpeed.png"; // 閫熷害琛ㄨ儗鏅浘锛岀洿鎺ユ浛鎹㈡垚浣犺嚜宸辩殑 PNG銆?
            style.useBackgroundImage = true; // false 鏃朵細鐢ㄧ函鑹查潰鏉垮崰浣嶏紝鏂逛究璋冨竷灞€銆?

            style.anchor = UiAnchor::BottomCenter; // 褰撳墠绀轰緥锛氬簳閮ㄥ眳涓€?
            style.alignment = ImVec2(0.5f, 1.0f); // 褰撳墠绀轰緥锛氫互閫熷害琛ㄥ簳杈逛腑蹇冧綔涓哄榻愮偣銆?
            style.offset = ImVec2(0.0f, -28.0f); // 寰€涓婃姮涓€鐐癸紝閬垮厤澶创搴曘€?

            // 涓嬮潰杩欎簺鏄父瑙佸榻愮ず渚嬶紝淇濈暀浣滃弬鑰冿細
            // style.anchor = UiAnchor::TopLeft;     style.alignment = ImVec2(0.0f, 0.0f); // 宸︿笂瑙掑榻?
            // style.anchor = UiAnchor::TopRight;    style.alignment = ImVec2(1.0f, 0.0f); // 鍙充笂瑙掑榻?
            // style.anchor = UiAnchor::Center;      style.alignment = ImVec2(0.5f, 0.5f); // 鏁翠釜閫熷害琛ㄥ眳涓?
            // style.anchor = UiAnchor::BottomLeft;  style.alignment = ImVec2(0.0f, 1.0f); // 宸︿笅瑙掕创杈?

            style.backgroundSize = ImVec2(320.0f, 160.0f); // 閫熷害琛ㄦ暣浣撳昂瀵搞€?
            style.backgroundScale = 1.0f; // 鎯虫暣浣撴斁澶у氨鏀硅繖閲屻€?
            style.backgroundTint = ImVec4(1.0f, 1.0f, 1.0f, 0.92f); // 鏀?alpha 鍙互璁╁簳鍥炬洿閫忔槑銆?
            style.fallbackPanelRounding = 18.0f; // 娌″浘鏃剁殑鍦嗚銆?

            style.speedMultiplier = 3.6f; // 褰撳墠鍋囪鍐呴儴閫熷害鏄?m/s锛屾墍浠ヨ浆 km/h銆?
            style.maxDisplaySpeed = 180.0f; // 涓昏鐢ㄤ簬鐧惧垎姣?鍔ㄧ敾寮哄害銆?
            style.useSmoothing = true; // 寮€鍚姩鐢诲钩婊戙€?
            style.smoothingFactor = 0.12f; // 瓒婂ぇ瓒婅窡鎵嬶紝瓒婂皬瓒娾€滄湁鎯€р€濄€?
            style.pulseWhenFast = true; // 楂橀€熸椂杞诲井鑴夊啿銆?
            style.pulseStartSpeed = 80.0f;
            style.pulseAmplitude = 0.045f;
            style.pulseSpeed = 6.0f;

            style.showTitle = true;
            style.titleText = "SPEED";
            style.titleFontSize = 18.0f;
            style.titleOffset = ImVec2(0.0f, 24.0f);

            style.numberFontSize = 46.0f; // 涓绘暟瀛楀ぇ灏忋€?
            style.numberFontScale = 1.0f; // 浜屾缂╂斁銆?
            style.numberOffset = ImVec2(0.0f, 64.0f); // 鏁板瓧鍦ㄨ儗鏅浘閲岀殑浣嶇疆銆?
            style.decimals = 0; // 鏄剧ず鏁存暟閫熷害銆?
            style.padWithZeros = false; // true 浼氭樉绀烘垚 005/042/120銆?
            style.minimumIntegerDigits = 3;

            style.showUnitLabel = true;
            style.unitLabel = "km/h";
            style.unitFontSize = 20.0f;
            style.unitOffset = ImVec2(0.0f, 108.0f);

            style.drawTextShadow = true; // 闃村奖澧炲己鍙鎬с€?
            style.shadowOffset = ImVec2(2.0f, 2.0f);
            style.drawTextOutline = true; // 鎻忚竟澧炲己浜儗鏅笅鐨勫彲璇绘€с€?
            style.outlineThickness = 1.0f;

            style.clipToViewport = true; // 涓嶈 UI 鐢诲嚭娓告垙鐢婚潰澶栥€?
            style.drawDebugBounds = false; // 璋冨竷灞€鏃跺彲鏀规垚 true 鐪嬭竟妗嗐€?
            style.safeMargin = 8.0f;

            return style;
        }

        HealthHudStyle BuildHealthHudStyle() {
            HealthHudStyle style{};
            return style;
        }

        void DrawHeartIcon(ImDrawList* drawList, const ImVec2& center, float size, ImU32 fillColor, ImU32 glossColor, ImU32 strokeColor) {
            if (!drawList || size <= 0.0f) {
                return;
            }

            const float radius = size * 0.34f;
            const ImVec2 leftCircle(center.x - radius * 0.95f, center.y - radius * 0.35f);
            const ImVec2 rightCircle(center.x + radius * 0.95f, center.y - radius * 0.35f);
            const ImVec2 bottomPoint(center.x, center.y + size * 0.78f);
            const ImVec2 leftShoulder(center.x - size * 0.70f, center.y + size * 0.02f);
            const ImVec2 rightShoulder(center.x + size * 0.70f, center.y + size * 0.02f);

            drawList->AddCircleFilled(leftCircle, radius, fillColor, 20);
            drawList->AddCircleFilled(rightCircle, radius, fillColor, 20);
            drawList->AddTriangleFilled(leftShoulder, rightShoulder, bottomPoint, fillColor);

            drawList->AddCircleFilled(
                ImVec2(center.x - radius * 0.55f, center.y - radius * 0.58f),
                radius * 0.48f,
                glossColor,
                16);

            drawList->AddCircle(leftCircle, radius, strokeColor, 20, 1.5f);
            drawList->AddCircle(rightCircle, radius, strokeColor, 20, 1.5f);
            drawList->AddTriangle(leftShoulder, rightShoulder, bottomPoint, strokeColor, 1.5f);
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

        void DrawHealthHud(const UserState& state, const ImVec2& viewportPos, const ImVec2& viewportSize) {
            if (viewportSize.x <= 1.0f || viewportSize.y <= 1.0f) {
                return;
            }

            const HealthHudStyle style = BuildHealthHudStyle();
            if (!style.enabled) {
                return;
            }
            const int maxLives = std::max(0, state.gameplay.maxLives);
            const int displayLives = std::clamp(state.gameplay.remainingLives, 0, maxLives);

            const ImVec2 anchorPoint = ResolveAnchorPoint(style.anchor, viewportPos, viewportSize);
            ImVec2 panelMin(
                anchorPoint.x - style.panelSize.x * style.alignment.x + style.offset.x,
                anchorPoint.y - style.panelSize.y * style.alignment.y + style.offset.y);

            if (style.safeMargin > 0.0f) {
                const float minX = viewportPos.x + style.safeMargin;
                const float minY = viewportPos.y + style.safeMargin;
                const float maxX = viewportPos.x + viewportSize.x - style.safeMargin - style.panelSize.x;
                const float maxY = viewportPos.y + viewportSize.y - style.safeMargin - style.panelSize.y;
                panelMin.x = std::clamp(panelMin.x, minX, std::max(minX, maxX));
                panelMin.y = std::clamp(panelMin.y, minY, std::max(minY, maxY));
            }

            const ImVec2 panelMax(panelMin.x + style.panelSize.x, panelMin.y + style.panelSize.y);

            ImGui::SetNextWindowBgAlpha(0.0f);
            ImGui::SetNextWindowPos(viewportPos, ImGuiCond_Always);
            ImGui::SetNextWindowSize(viewportSize, ImGuiCond_Always);

            ImGuiWindowFlags overlayFlags = ImGuiWindowFlags_NoDecoration
                | ImGuiWindowFlags_NoDocking
                | ImGuiWindowFlags_NoSavedSettings
                | ImGuiWindowFlags_NoFocusOnAppearing
                | ImGuiWindowFlags_NoInputs
                | ImGuiWindowFlags_NoNav
                | ImGuiWindowFlags_NoMove;

            if (!ImGui::Begin("Game HUD Overlay###HealthHud", nullptr, overlayFlags)) {
                ImGui::End();
                return;
            }

            ImDrawList* drawList = ImGui::GetWindowDrawList();
            if (style.clipToViewport) {
                drawList->PushClipRect(viewportPos, ImVec2(viewportPos.x + viewportSize.x, viewportPos.y + viewportSize.y), true);
            }

            drawList->AddRectFilled(panelMin, panelMax, ToU32(style.panelColor), style.panelRounding);
            drawList->AddRect(panelMin, panelMax, ToU32(style.panelBorderColor), style.panelRounding, 0, style.panelBorderThickness);

            ImFont* baseFont = ImGui::GetFont();
            const ImVec2 titlePos(panelMin.x + style.titleOffset.x, panelMin.y + style.titleOffset.y);
            DrawStyledText(drawList, baseFont, style.titleFontSize, titlePos, ToU32(style.titleColor), style.titleText, style);

            for (int i = 0; i < maxLives; ++i) {
                const float heartCenterX = panelMin.x + style.heartsOffset.x + style.heartSize + i * (style.heartSize * 2.0f + style.heartSpacing);
                const float heartCenterY = panelMin.y + style.heartsOffset.y + style.heartSize;
                const bool alive = i < displayLives;
                DrawHeartIcon(
                    drawList,
                    ImVec2(heartCenterX, heartCenterY),
                    style.heartSize,
                    ToU32(alive ? style.heartFillColor : style.heartEmptyColor),
                    ToU32(alive ? style.heartGlossColor : ImVec4(0.0f, 0.0f, 0.0f, 0.0f)),
                    ToU32(style.heartStrokeColor));
            }

            char livesText[16] = {};
            std::snprintf(livesText, sizeof(livesText), "x%d", displayLives);
            const ImVec2 labelAnchor(panelMin.x + style.labelOffset.x, panelMin.y + style.labelOffset.y);
            const ImVec2 labelPos = CalculateTextPosition(baseFont, style.labelFontSize, livesText, labelAnchor, style.centerLabel);
            DrawStyledText(drawList, baseFont, style.labelFontSize, labelPos, ToU32(style.labelColor), livesText, style);

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
        DrawHealthHud(state, viewportPos, viewportSize);
        DrawImageSpeedometerDemo(renderSys, state, viewportPos, viewportSize);
        DrawViewportHint(state, viewportPos, viewportSize);
    }
} // namespace engine
