#include "RuntimeUIDebugPanel.hpp"

#include <algorithm>
#include <array>
#include <format>
#include <vector>

#include "../../UserState/UserState.hpp"
#include "../EngineUi.hpp"
#include "UIAnimation.hpp"
#include "UIElement.hpp"
#include "UIManager.hpp"
#include "UIScreen.hpp"
#include "UITransform.hpp"

namespace engine {
    namespace RuntimeUIDebugPanel {

        namespace {

            const char* GameFlowStateName(GameFlowState state) {
                switch (state) {
                case GameFlowState::MainMenu: return "MainMenu";
                case GameFlowState::Playing:  return "Playing";
                case GameFlowState::Paused:   return "Paused";
                case GameFlowState::Settings: return "Settings";
                case GameFlowState::GameOver: return "GameOver";
                }
                return "Unknown";
            }

            // 把 UI 元素的运行时矩形展开成视口坐标，给 overlay 使用。
            struct DebugRect {
                const UIScreen* screen = nullptr;
                const UIElement* element = nullptr;
                UIRect rect{};
                bool interactive = false;
            };

            bool IsInteractive(const UIElement& element) {
                return element.visible && element.enabled && element.interactable &&
                    (element.GetType() == UIElementType::Button ||
                        element.GetType() == UIElementType::Toggle ||
                        element.GetType() == UIElementType::Slider ||
                        element.GetType() == UIElementType::InputField);
            }

            void CollectDebugRects(const UIScreen& screen,
                                   const UIElement& element,
                                   const UIRect& parentRect,
                                   std::vector<DebugRect>& outRects) {
                if (element.GetType() != UIElementType::Canvas && !element.visible) {
                    return;
                }

                const UIRect elementRect = element.GetType() == UIElementType::Canvas
                    ? parentRect
                    : element.transform.ComputeRect(parentRect);

                if (element.GetType() != UIElementType::Canvas) {
                    outRects.push_back(DebugRect{ &screen, &element, elementRect, IsInteractive(element) });
                }

                for (const auto& child : element.GetChildren()) {
                    CollectDebugRects(screen, *child, elementRect, outRects);
                }
            }

        } // namespace

        std::string FormatUiValueForDebug(const UIValue* value) {
            if (!value) {
                return "<missing>";
            }
            if (const auto* boolValue = std::get_if<bool>(&value->data)) {
                return *boolValue ? "true" : "false";
            }
            if (const auto* intValue = std::get_if<int>(&value->data)) {
                return std::format("{}", *intValue);
            }
            if (const auto* floatValue = std::get_if<float>(&value->data)) {
                return std::format("{:.3f}", *floatValue);
            }
            if (const auto* stringValue = std::get_if<std::string>(&value->data)) {
                return *stringValue;
            }
            if (const auto* vec2Value = std::get_if<glm::vec2>(&value->data)) {
                return std::format("({:.2f}, {:.2f})", vec2Value->x, vec2Value->y);
            }
            if (const auto* colorValue = std::get_if<glm::vec4>(&value->data)) {
                return std::format("({:.2f}, {:.2f}, {:.2f}, {:.2f})",
                    colorValue->x, colorValue->y, colorValue->z, colorValue->w);
            }
            return "<none>";
        }

        void DrawRuntimeStackOverlay(const UIManager& uiManager,
                                     const UIRenderContext& context,
                                     ImVec2 canvasMin,
                                     ImVec2 /*canvasMax*/,
                                     bool showHudDataContext) {
            ImDrawList* drawList = static_cast<ImDrawList*>(context.nativeContext);
            if (!drawList) {
                return;
            }

            // 屏幕栈快览框 ——
            // 让美术 / 策划 在演示时一眼看到 MainMenu / HUD / Pause 的叠放顺序。
            const std::vector<std::string> visibleScreens = uiManager.GetVisibleScreenNames();
            if (!visibleScreens.empty()) {
                const ImVec2 origin(canvasMin.x + 12.0f, canvasMin.y + 12.0f);
                const float lineHeight = ImGui::GetTextLineHeightWithSpacing();
                float maxWidth = ImGui::CalcTextSize("Runtime UI Stack").x;
                for (const std::string& screenName : visibleScreens) {
                    maxWidth = std::max(maxWidth, ImGui::CalcTextSize(screenName.c_str()).x);
                }

                const float boxWidth = maxWidth + 20.0f;
                const float boxHeight = (visibleScreens.size() + 1) * lineHeight + 14.0f;
                drawList->AddRectFilled(
                    origin,
                    ImVec2(origin.x + boxWidth, origin.y + boxHeight),
                    IM_COL32(8, 12, 18, 180),
                    8.0f);
                drawList->AddRect(
                    origin,
                    ImVec2(origin.x + boxWidth, origin.y + boxHeight),
                    IM_COL32(120, 160, 220, 210),
                    8.0f, 0, 1.0f);

                ImVec2 cursor(origin.x + 10.0f, origin.y + 6.0f);
                drawList->AddText(cursor, IM_COL32(230, 235, 255, 255), "Runtime UI Stack");
                cursor.y += lineHeight;
                for (const std::string& screenName : visibleScreens) {
                    drawList->AddText(cursor, IM_COL32(220, 225, 235, 255), screenName.c_str());
                    cursor.y += lineHeight;
                }
            }

            if (!showHudDataContext) {
                return;
            }

            // HUD DataContext 快览 ——
            // 用户开了 ui.showDebugHud 才显示，常驻在屏幕栈下方。
            const UIDataContext& dataContext = uiManager.GetDataContext();
            const ImVec2 origin(canvasMin.x + 12.0f, canvasMin.y + 110.0f);
            const float lineHeight = ImGui::GetTextLineHeightWithSpacing();
            const float boxWidth = 290.0f;
            const float boxHeight = 9.0f * lineHeight + 14.0f;
            drawList->AddRectFilled(
                origin,
                ImVec2(origin.x + boxWidth, origin.y + boxHeight),
                IM_COL32(8, 12, 18, 180),
                8.0f);
            drawList->AddRect(
                origin,
                ImVec2(origin.x + boxWidth, origin.y + boxHeight),
                IM_COL32(120, 200, 140, 210),
                8.0f, 0, 1.0f);

            auto tryGetFloat = [&dataContext](const char* key, float fallback = 0.0f) {
                if (const UIValue* value = dataContext.GetValue(key)) {
                    if (const auto* fv = std::get_if<float>(&value->data)) return *fv;
                    if (const auto* iv = std::get_if<int>(&value->data)) return static_cast<float>(*iv);
                }
                return fallback;
            };
            auto tryGetInt = [&dataContext](const char* key, int fallback = 0) {
                if (const UIValue* value = dataContext.GetValue(key)) {
                    if (const auto* iv = std::get_if<int>(&value->data)) return *iv;
                    if (const auto* fv = std::get_if<float>(&value->data)) return static_cast<int>(*fv);
                }
                return fallback;
            };
            auto tryGetBool = [&dataContext](const char* key, bool fallback = false) {
                if (const UIValue* value = dataContext.GetValue(key)) {
                    if (const auto* bv = std::get_if<bool>(&value->data)) return *bv;
                }
                return fallback;
            };
            auto tryGetString = [&dataContext](const char* key, std::string fallback = {}) {
                if (const UIValue* value = dataContext.GetValue(key)) {
                    if (const auto* sv = std::get_if<std::string>(&value->data)) return *sv;
                }
                return fallback;
            };

            ImVec2 cursor(origin.x + 10.0f, origin.y + 6.0f);
            drawList->AddText(cursor, IM_COL32(230, 255, 235, 255), "HUD DataContext");
            cursor.y += lineHeight;
            const std::array<std::string, 8> lines = {
                std::format("speed: {:.1f} km/h", tryGetFloat("bike.speedKmh")),
                std::format("gear: {}", tryGetInt("bike.gear")),
                std::format("energy: {:.2f}", tryGetFloat("bike.energy")),
                std::format("stamina: {:.2f}", tryGetFloat("bike.stamina")),
                std::format("distance: {:.1f} m", tryGetFloat("bike.distance")),
                std::format("lap: {}", tryGetString("race.lapTime")),
                std::format("current lap: {}", tryGetInt("race.currentLap", 1)),
                std::format("low energy: {}", tryGetBool("bike.isLowEnergy") ? "true" : "false")
            };
            for (const std::string& line : lines) {
                drawList->AddText(cursor, IM_COL32(220, 225, 235, 255), line.c_str());
                cursor.y += lineHeight;
            }
        }

        void DrawDebugOverlay(const DebugState& state,
                              const UIManager& uiManager,
                              const UIRenderContext& context,
                              ImVec2 canvasMin,
                              ImVec2 canvasMax) {
            ImDrawList* drawList = static_cast<ImDrawList*>(context.nativeContext);
            if (!drawList) {
                return;
            }

            drawList->PushClipRect(canvasMin, canvasMax, true);
            const UIDataContext& dataContext = uiManager.GetDataContext();

            for (const auto& loadedScreen : uiManager.GetLoadedScreens()) {
                if (!loadedScreen.screen || !loadedScreen.screen->IsVisible() || !loadedScreen.screen->GetRootCanvas()) {
                    continue;
                }

                std::vector<DebugRect> rects;
                rects.reserve(64);
                const UIRect rootRect{ glm::vec2(0.0f), loadedScreen.screen->GetReferenceResolution(), 0.0f };
                CollectDebugRects(*loadedScreen.screen, *loadedScreen.screen->GetRootCanvas(), rootRect, rects);

                for (const DebugRect& entry : rects) {
                    const ImVec2 min(
                        context.rootPosition.x + entry.rect.position.x * context.axisScale.x,
                        context.rootPosition.y + entry.rect.position.y * context.axisScale.y);
                    const ImVec2 max(
                        context.rootPosition.x + (entry.rect.position.x + entry.rect.size.x) * context.axisScale.x,
                        context.rootPosition.y + (entry.rect.position.y + entry.rect.size.y) * context.axisScale.y);

                    if (state.showBounds) {
                        drawList->AddRect(min, max, IM_COL32(80, 220, 140, 210), 0.0f, 0, 1.0f);
                    }
                    if (state.showHitRects && entry.interactive) {
                        drawList->AddRect(min, max, IM_COL32(255, 96, 96, 220), 0.0f, 0, 2.0f);
                    }
                    if (state.selectedElementId != 0 && entry.element &&
                        entry.element->GetId() == state.selectedElementId) {
                        drawList->AddRect(min, max, IM_COL32(255, 215, 96, 255), 0.0f, 0, 2.5f);
                    }
                }

                if (state.showBindingValues &&
                    !state.selectedScreenName.empty() &&
                    loadedScreen.screen->GetName() == state.selectedScreenName &&
                    state.selectedElementId != 0) {
                    const UIElement* selectedElement = loadedScreen.screen->FindById(state.selectedElementId);
                    if (!selectedElement) {
                        continue;
                    }
                    for (const DebugRect& entry : rects) {
                        if (!entry.element || entry.element->GetId() != state.selectedElementId) {
                            continue;
                        }
                        ImVec2 textPos(
                            context.rootPosition.x + entry.rect.position.x * context.axisScale.x + 6.0f,
                            context.rootPosition.y + entry.rect.position.y * context.axisScale.y - 18.0f);
                        for (const UIPropertyBinding& binding : loadedScreen.screen->GetBindings()) {
                            if (binding.targetElementId != state.selectedElementId) {
                                continue;
                            }
                            const std::string line = std::format(
                                "{} = {}",
                                binding.sourceKey,
                                FormatUiValueForDebug(dataContext.GetValue(binding.sourceKey)));
                            drawList->AddText(textPos, IM_COL32(255, 245, 200, 255), line.c_str());
                            textPos.y -= ImGui::GetTextLineHeightWithSpacing();
                        }
                        break;
                    }
                }
            }

            drawList->PopClipRect();
        }

        bool HandleViewportPick(DebugState& state,
                                const UIManager& uiManager,
                                const UIRenderContext& context,
                                ImVec2 canvasMin,
                                ImVec2 canvasMax,
                                ImVec2 mousePosition) {
            const bool isInsideCanvas =
                mousePosition.x >= canvasMin.x && mousePosition.x <= canvasMax.x &&
                mousePosition.y >= canvasMin.y && mousePosition.y <= canvasMax.y;
            if (!isInsideCanvas) {
                return false;
            }

            const glm::vec2 runtimeMouse(
                (mousePosition.x - context.rootPosition.x) / context.axisScale.x,
                (mousePosition.y - context.rootPosition.y) / context.axisScale.y);

            std::string screenName;
            UIElementId elementId = 0;
            const UIElement* hitElement = uiManager.DebugHitTestElement(runtimeMouse, &screenName, &elementId);
            if (!hitElement) {
                state.selectedElementId = 0;
                state.selectedScreenName.clear();
                return false;
            }

            state.selectedElementId = elementId;
            state.selectedScreenName = screenName;
            return true;
        }

        void DrawDebugPanel(DebugState& state,
                            UIManager& uiManager,
                            const UserState& userState) {
            // 选中目标如果已经被销毁就立刻清空，避免后面的查询拿到悬空指针。
            if (state.selectedElementId != 0 && !state.selectedScreenName.empty()) {
                const UIScreen* selectedScreen = uiManager.GetScreen(state.selectedScreenName);
                if (!selectedScreen || !selectedScreen->IsVisible() || !selectedScreen->FindById(state.selectedElementId)) {
                    state.selectedElementId = 0;
                    state.selectedScreenName.clear();
                }
            }

            UIManager::UISettings& settings = uiManager.GetSettings();
            const UIDataContext& dataContext = uiManager.GetDataContext();

            ImGui::SetNextWindowPos(ImVec2(1120, 140), ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowSize(ImVec2(420, 620), ImGuiCond_FirstUseEver);
            if (!ImGui::Begin("Runtime UI Debug", nullptr)) {
                ImGui::End();
                return;
            }

            // ---------- 游戏流程状态 + 活跃屏幕 ----------
            ImGui::TextUnformatted("Game Flow");
            ImGui::Text("State: %s", GameFlowStateName(userState.gameFlowState));
            ImGui::Text("Started=%s  Pause=%s  GameOver=%s",
                userState.isGameStarted ? "true" : "false",
                userState.isGamePause ? "true" : "false",
                userState.isGameOver ? "true" : "false");

            ImGui::Separator();
            ImGui::TextUnformatted("Active Runtime UI Screens");
            for (const auto& loadedScreen : uiManager.GetLoadedScreens()) {
                if (!loadedScreen.screen) {
                    continue;
                }
                ImGui::BulletText(
                    "%s [%s] order=%d",
                    loadedScreen.screen->GetName().c_str(),
                    loadedScreen.screen->IsVisible() ? "Visible" : "Hidden",
                    loadedScreen.screen->GetRenderOrder());
            }

            // ---------- 调试开关 ----------
            ImGui::Separator();
            ImGui::TextUnformatted("Runtime UI Debug Toggles");
            ImGui::Checkbox("Pick Runtime UI In Viewport", &state.enablePicking);
            ImGui::Checkbox("Show Live Runtime UI", &state.showLivePreview);
            ImGui::Checkbox("Show Runtime UI Bounds", &state.showBounds);
            ImGui::Checkbox("Show Hit Test Rects", &state.showHitRects);
            ImGui::Checkbox("Show Binding Debug Values", &state.showBindingValues);
            ImGui::Checkbox("Show Animation Debug", &state.showAnimationDebug);
            ImGui::Checkbox("Show Event Log", &state.showEventLog);
            ImGui::Checkbox("Show DataContext Dump", &state.showDataContext);

            // ---------- 全局参数 ----------
            ImGui::Separator();
            ImGui::TextUnformatted("Runtime UI Parameters");
            ImGui::SliderFloat("ui.globalScale", &settings.globalScale, 0.5f, 2.5f, "%.2f");
            ImGui::SliderFloat("ui.hudOpacity", &settings.hudOpacity, 0.0f, 1.0f, "%.2f");
            ImGui::SliderFloat("ui.speedTextScale", &settings.speedTextScale, 0.5f, 2.5f, "%.2f");
            ImGui::Checkbox("ui.showDebugHud", &settings.showDebugHud);
            ImGui::SliderFloat("ui.animationSpeed", &settings.animationSpeed, 0.1f, 3.0f, "%.2f");
            if (ImGui::Button("Save Runtime UI Debug Settings")) {
                EngineUi::LogPrint("[RuntimeUI] Debug settings save is not persistent yet. Current changes remain session-only.\n");
            }

            // ---------- 数据上下文键值快照 ----------
            if (state.showDataContext) {
                ImGui::Separator();
                ImGui::Text("DataContext (%zu keys)", dataContext.Size());
                if (ImGui::BeginChild("##uiDataContextDump", ImVec2(0.0f, 140.0f), true)) {
                    for (const std::string& key : dataContext.CollectKeys()) {
                        ImGui::TextUnformatted(std::format("{} = {}",
                            key,
                            FormatUiValueForDebug(dataContext.GetValue(key))).c_str());
                    }
                }
                ImGui::EndChild();
            }

            // ---------- 最近的 UI 事件 ----------
            if (state.showEventLog) {
                ImGui::Separator();
                ImGui::Text("Recent UI Events (%zu)", uiManager.GetRecentEvents().size());
                if (ImGui::Button("Clear Events")) {
                    uiManager.ClearRecentEvents();
                }
                if (ImGui::BeginChild("##uiRecentEvents", ImVec2(0.0f, 120.0f), true)) {
                    const auto& events = uiManager.GetRecentEvents();
                    for (auto it = events.rbegin(); it != events.rend(); ++it) {
                        const ImVec4 color = it->hadHandler
                            ? ImVec4(0.85f, 1.0f, 0.85f, 1.0f)
                            : ImVec4(1.0f, 0.7f, 0.6f, 1.0f);
                        ImGui::TextColored(color, "#%llu  %s%s",
                            static_cast<unsigned long long>(it->sequence),
                            it->eventName.c_str(),
                            it->hadHandler ? "" : "  (no handler)");
                    }
                }
                ImGui::EndChild();
            }

            // ---------- 选中元素 ----------
            ImGui::Separator();
            ImGui::TextUnformatted("Selected Runtime UI Element");
            if (state.selectedElementId == 0 || state.selectedScreenName.empty()) {
                ImGui::TextDisabled("Click a runtime UI element in the viewport to inspect it.");
            }
            else {
                const UIScreen* selectedScreen = uiManager.GetScreen(state.selectedScreenName);
                const UIElement* selectedElement = selectedScreen ? selectedScreen->FindById(state.selectedElementId) : nullptr;
                if (!selectedScreen || !selectedElement) {
                    ImGui::TextDisabled("Selected runtime UI element is no longer available.");
                }
                else {
                    ImGui::Text("Screen: %s", selectedScreen->GetName().c_str());
                    ImGui::Text("Name: %s", selectedElement->GetName().c_str());
                    ImGui::Text("ID: %llu", static_cast<unsigned long long>(selectedElement->GetId()));
                    ImGui::Text("Type: %s", ToString(selectedElement->GetType()).data());
                    ImGui::Text("Visible / Enabled / Interactable: %s / %s / %s",
                        selectedElement->visible ? "true" : "false",
                        selectedElement->enabled ? "true" : "false",
                        selectedElement->interactable ? "true" : "false");
                    ImGui::Text("Position: (%.1f, %.1f)",
                        selectedElement->transform.position.x, selectedElement->transform.position.y);
                    ImGui::Text("Size: (%.1f, %.1f)",
                        selectedElement->transform.size.x, selectedElement->transform.size.y);
                    ImGui::Text("Opacity: %.2f", selectedElement->style.opacity);
                    ImGui::Text("Font Size: %.1f", selectedElement->style.fontSize);
                    glm::vec4 tintColor = selectedElement->style.tintColor;
                    glm::vec4 textColor = selectedElement->style.textColor;
                    ImGui::ColorEdit4("Tint Color", &tintColor.x, ImGuiColorEditFlags_NoInputs);
                    ImGui::ColorEdit4("Text Color", &textColor.x, ImGuiColorEditFlags_NoInputs);

                    ImGui::Separator();
                    ImGui::TextUnformatted("Bindings");
                    bool hasBinding = false;
                    for (const UIPropertyBinding& binding : selectedScreen->GetBindings()) {
                        if (binding.targetElementId != selectedElement->GetId()) {
                            continue;
                        }
                        hasBinding = true;
                        ImGui::BulletText("%s <- %s",
                            ToString(binding.targetProperty).data(),
                            binding.sourceKey.c_str());
                        ImGui::Text("Current Value: %s",
                            FormatUiValueForDebug(dataContext.GetValue(binding.sourceKey)).c_str());
                    }
                    if (!hasBinding) {
                        ImGui::TextDisabled("No bindings on selected element.");
                    }

                    ImGui::Separator();
                    ImGui::TextUnformatted("Active Animations");
                    if (const UIAnimator* animator = uiManager.GetAnimator(selectedScreen->GetName())) {
                        if (!state.showAnimationDebug) {
                            ImGui::TextDisabled("Animation debug display is disabled.");
                        }
                        else if (animator->GetActiveClips().empty()) {
                            ImGui::TextDisabled("No active animations.");
                        }
                        else {
                            for (const UIAnimator::ActiveClipState& activeClip : animator->GetActiveClips()) {
                                ImGui::BulletText(
                                    "%s | t=%.2f | loops=%d%s",
                                    activeClip.clipName.c_str(),
                                    activeClip.currentTime,
                                    activeClip.completedLoops,
                                    activeClip.paused ? " | paused" : "");
                            }
                        }
                    }
                    else {
                        ImGui::TextDisabled("Animator unavailable for this screen.");
                    }
                }
            }

            ImGui::End();
        }

    } // namespace RuntimeUIDebugPanel
} // namespace engine
