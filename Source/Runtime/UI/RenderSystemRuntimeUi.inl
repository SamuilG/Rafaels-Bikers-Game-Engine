#pragma once

inline bool RenderSystem::HasRuntimeUiScreen() const {
    return mRuntimeUiManager && mRuntimeUiManager->HasVisibleScreen() && mRuntimeUiManager->GetActiveScreen();
}

inline bool RenderSystem::ShouldRenderRuntimeUi() const {
    return mState && mState->showRuntimeUi && !mState->showEngineUi && HasRuntimeUiScreen();
}

inline bool RenderSystem::ShouldRenderRuntimeUiDebugPreview() const {
    return mState &&
        mState->showRuntimeUi &&
        mState->showEngineUi &&
        mState->showDebugPanel &&
        mRuntimeUiDebugShowLivePreview &&
        HasRuntimeUiScreen();
}

inline bool RenderSystem::BuildRuntimeUiRenderContext(UIRenderContext& outContext, ImVec2& outCanvasMin, ImVec2& outCanvasMax) const {
    if (!HasRuntimeUiScreen()) {
        return false;
    }

    const ImVec2 viewportPos = EngineUi::GetSceneViewportPos();
    const ImVec2 viewportSize = EngineUi::GetSceneViewportSize();
    if (viewportSize.x <= 1.0f || viewportSize.y <= 1.0f) {
        return false;
    }

    const glm::vec2 referenceResolution = mRuntimeUiManager->GetActiveScreen()->GetReferenceResolution();
    if (referenceResolution.x <= 0.0f || referenceResolution.y <= 0.0f) {
        return false;
    }

    const float scaleX = viewportSize.x / referenceResolution.x;
    const float scaleY = viewportSize.y / referenceResolution.y;
    const float globalScale = mRuntimeUiManager
        ? std::clamp(mRuntimeUiManager->GetSettings().globalScale, 0.1f, 4.0f)
        : 1.0f;
    const glm::vec2 axisScale(scaleX * globalScale, scaleY * globalScale);
    const float scale = std::min(axisScale.x, axisScale.y);
    if (!std::isfinite(scale) || scale <= 0.0f ||
        !std::isfinite(axisScale.x) || !std::isfinite(axisScale.y) ||
        axisScale.x <= 0.0f || axisScale.y <= 0.0f) {
        return false;
    }

    const glm::vec2 viewportSizeVec(viewportSize.x, viewportSize.y);
    const glm::vec2 canvasSize(referenceResolution.x * axisScale.x, referenceResolution.y * axisScale.y);
    const glm::vec2 rootPosition = glm::vec2(viewportPos.x, viewportPos.y) + (viewportSizeVec - canvasSize) * 0.5f;

    outContext.rootPosition = rootPosition;
    outContext.targetSize = canvasSize;
    outContext.axisScale = axisScale;
    outContext.scale = scale;
    outContext.opacityMultiplier = 1.0f;
    outContext.hudOpacityMultiplier = mRuntimeUiManager ? std::clamp(mRuntimeUiManager->GetSettings().hudOpacity, 0.0f, 1.0f) : 1.0f;
    outContext.speedTextScale = mRuntimeUiManager ? std::clamp(mRuntimeUiManager->GetSettings().speedTextScale, 0.1f, 4.0f) : 1.0f;
    outContext.selectedElementId = mRuntimeUiDebugSelectedElementId;
    ImDrawList* runtimeUiDrawList = nullptr;
    if (mState && mState->showEngineUi) {
        runtimeUiDrawList = EngineUi::GetSceneViewportDrawList();
    }
    if (!runtimeUiDrawList) {
        runtimeUiDrawList = ImGui::GetForegroundDrawList(ImGui::GetMainViewport());
    }
    outContext.nativeContext = runtimeUiDrawList;

    outCanvasMin = ImVec2(rootPosition.x, rootPosition.y);
    outCanvasMax = ImVec2(rootPosition.x + canvasSize.x, rootPosition.y + canvasSize.y);
    return true;
}

inline void RenderSystem::RefreshRuntimeUiDataContext(float dt) {
    if (!mRuntimeUiManager || !mState) {
        return;
    }

    if (!mState->isGameStarted || mState->isGameOver || mState->isGameWon) {
        mRuntimeUiLapTimeSeconds = 0.0f;
        mRuntimeUiTravelDistanceMeters = 0.0f;
    }
    else if (!mState->isGamePause) {
        mRuntimeUiLapTimeSeconds += dt;
        mRuntimeUiTravelDistanceMeters += (std::abs(mState->bikeSpeed) / 3.6f) * dt;
    }

    const float speedKmh = std::abs(mState->bikeSpeed);
    const float speedMph = speedKmh * 0.621371f;
    
    const float energy = std::clamp(1.0f - (speedKmh / 60.0f), 0.0f, 1.0f);
    const float stamina = energy;
    const bool isLowEnergy = energy <= 0.25f;
    const glm::vec4 warningColor = isLowEnergy
        ? glm::vec4(1.0f, 0.26f, 0.22f, 1.0f)
        : glm::vec4(1.0f, 1.0f, 1.0f, 0.18f);

    const float distanceMeters = mRuntimeUiTravelDistanceMeters;
    const int currentLap = 1;
    auto& settings = mRuntimeUiManager->GetSettings();

    auto& dataContext = mRuntimeUiManager->GetDataContext();
    dataContext.SetFloat("bike.speedKmh", speedKmh);
    dataContext.SetFloat("bike.speedMph", speedMph);
    
    dataContext.SetFloat("bike.energy", energy);
    dataContext.SetFloat("bike.stamina", stamina);
    dataContext.SetFloat("bike.distance", distanceMeters);
    dataContext.SetBool("bike.isLowEnergy", isLowEnergy);
    dataContext.SetBool("bike.lowStamina", isLowEnergy);
    dataContext.SetColor("bike.warningColor", warningColor);
    dataContext.SetFloat("race.lapTimeSeconds", mRuntimeUiLapTimeSeconds);
    dataContext.SetFloat("race.bestLapTimeSeconds", mRuntimeUiLapTimeSeconds);
    dataContext.SetInt("race.currentLap", currentLap);
    dataContext.SetFloat("ui.globalScale", settings.globalScale);
    dataContext.SetFloat("ui.hudOpacity", settings.hudOpacity);
    dataContext.SetFloat("ui.speedTextScale", settings.speedTextScale);
    dataContext.SetBool("ui.showDebugHud", settings.showDebugHud);
    dataContext.SetFloat("ui.animationSpeed", settings.animationSpeed);

    const int totalMilliseconds = static_cast<int>(std::round(mRuntimeUiLapTimeSeconds * 1000.0f));
    const int minutes = totalMilliseconds / 60000;
    const int seconds = (totalMilliseconds / 1000) % 60;
    const int milliseconds = totalMilliseconds % 1000;
    const std::string formattedLapTime = std::format("{:02}:{:02}.{:03}", minutes, seconds, milliseconds);
    dataContext.SetString("race.lapTime", formattedLapTime);
    dataContext.SetString("race.bestLapTime", formattedLapTime);
}

inline void RenderSystem::UpdateRuntimeUi(float dt) {
    if (!mRuntimeUiManager) {
        return;
    }

    RefreshRuntimeUiDataContext(dt);

    if (ShouldRenderRuntimeUi() || ShouldRenderRuntimeUiDebugPreview()) {
        mRuntimeUiManager->Update(dt);
    }
    else {
        mRuntimeUiManager->ClearInputState();
    }
}

inline void RenderSystem::ForwardRuntimeUiMouseInput() {
    if (!mRuntimeUiManager) {
        return;
    }

    if (!ShouldRenderRuntimeUi()) {
        mRuntimeUiManager->ClearInputState();
        return;
    }

    UIRenderContext context{};
    ImVec2 canvasMin{};
    ImVec2 canvasMax{};
    if (!BuildRuntimeUiRenderContext(context, canvasMin, canvasMax)) {
        mRuntimeUiManager->ClearInputState();
        return;
    }

    const ImVec2 mousePosition = ImGui::GetMousePos();
    const bool isInsideCanvas =
        mousePosition.x >= canvasMin.x && mousePosition.x <= canvasMax.x &&
        mousePosition.y >= canvasMin.y && mousePosition.y <= canvasMax.y;

    if (isInsideCanvas) {
        const glm::vec2 runtimeMouse(
            (mousePosition.x - context.rootPosition.x) / context.axisScale.x,
            (mousePosition.y - context.rootPosition.y) / context.axisScale.y);
        mRuntimeUiManager->HandleMouseMove(runtimeMouse);
    }
    else {
        mRuntimeUiManager->HandleMouseMove(glm::vec2(-10000.0f));
    }

    if (isInsideCanvas && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        mRuntimeUiManager->HandleMouseDown();
    }

    if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
        mRuntimeUiManager->HandleMouseUp();
    }

    ImGuiIO& io = ImGui::GetIO();
    std::string inputText;
    inputText.reserve(io.InputQueueCharacters.Size);
    auto appendUtf8 = [&inputText](ImWchar character) {
        if (character <= 0x7F) {
            inputText.push_back(static_cast<char>(character));
        }
        else if (character <= 0x7FF) {
            inputText.push_back(static_cast<char>(0xC0 | ((character >> 6) & 0x1F)));
            inputText.push_back(static_cast<char>(0x80 | (character & 0x3F)));
        }
        else if (character <= 0xFFFF) {
            inputText.push_back(static_cast<char>(0xE0 | ((character >> 12) & 0x0F)));
            inputText.push_back(static_cast<char>(0x80 | ((character >> 6) & 0x3F)));
            inputText.push_back(static_cast<char>(0x80 | (character & 0x3F)));
        }
        else {
            inputText.push_back(static_cast<char>(0xF0 | ((character >> 18) & 0x07)));
            inputText.push_back(static_cast<char>(0x80 | ((character >> 12) & 0x3F)));
            inputText.push_back(static_cast<char>(0x80 | ((character >> 6) & 0x3F)));
            inputText.push_back(static_cast<char>(0x80 | (character & 0x3F)));
        }
    };

    for (ImWchar character : io.InputQueueCharacters) {
        if (character < 32 || character == 127) {
            continue;
        }
        appendUtf8(character);
    }
    if (!inputText.empty()) {
        mRuntimeUiManager->HandleTextInput(inputText);
    }

    if (ImGui::IsKeyPressed(ImGuiKey_Backspace, false)) {
        mRuntimeUiManager->HandleBackspace();
    }

}

inline void RenderSystem::RenderRuntimeUi() {
    if (!mRuntimeUiManager || (!ShouldRenderRuntimeUi() && !ShouldRenderRuntimeUiDebugPreview())) {
        return;
    }

    UIRenderContext context{};
    ImVec2 canvasMin{};
    ImVec2 canvasMax{};
    if (!BuildRuntimeUiRenderContext(context, canvasMin, canvasMax)) {
        return;
    }

    ImDrawList* drawList = static_cast<ImDrawList*>(context.nativeContext);
    if (!drawList) {
        return;
    }

    drawList->PushClipRect(canvasMin, canvasMax, true);
    mRuntimeUiManager->Render(context);

    const std::vector<std::string> visibleScreens = mRuntimeUiManager->GetVisibleScreenNames();
    
    if (mRuntimeUiDebugShowStack && !visibleScreens.empty()) {
        const ImVec2 debugOrigin(canvasMin.x + 12.0f, canvasMin.y + 12.0f);
        const float lineHeight = ImGui::GetTextLineHeightWithSpacing();
        float maxWidth = ImGui::CalcTextSize("Runtime UI Stack").x;
        for (const std::string& screenName : visibleScreens) {
            maxWidth = std::max(maxWidth, ImGui::CalcTextSize(screenName.c_str()).x);
        }

        const float boxWidth = maxWidth + 20.0f;
        const float boxHeight = (visibleScreens.size() + 1) * lineHeight + 14.0f;
        drawList->AddRectFilled(
            debugOrigin,
            ImVec2(debugOrigin.x + boxWidth, debugOrigin.y + boxHeight),
            IM_COL32(8, 12, 18, 180),
            8.0f);
        drawList->AddRect(
            debugOrigin,
            ImVec2(debugOrigin.x + boxWidth, debugOrigin.y + boxHeight),
            IM_COL32(120, 160, 220, 210),
            8.0f,
            0,
            1.0f);

        ImVec2 textCursor(debugOrigin.x + 10.0f, debugOrigin.y + 6.0f);
        drawList->AddText(textCursor, IM_COL32(230, 235, 255, 255), "Runtime UI Stack");
        textCursor.y += lineHeight;
        for (const std::string& screenName : visibleScreens) {
            drawList->AddText(textCursor, IM_COL32(220, 225, 235, 255), screenName.c_str());
            textCursor.y += lineHeight;
        }
    }

    if (mRuntimeUiManager->GetSettings().showDebugHud) {
        const UIDataContext& dataContext = mRuntimeUiManager->GetDataContext();
        const ImVec2 dataOrigin(canvasMin.x + 12.0f, canvasMin.y + 110.0f);
        const float dataLineHeight = ImGui::GetTextLineHeightWithSpacing();
        const char* dataPanelTitle = "HUD DataContext";
        const float dataBoxWidth = 290.0f;
        const float dataBoxHeight = 9.0f * dataLineHeight + 14.0f;
        drawList->AddRectFilled(
            dataOrigin,
            ImVec2(dataOrigin.x + dataBoxWidth, dataOrigin.y + dataBoxHeight),
            IM_COL32(8, 12, 18, 180),
            8.0f);
        drawList->AddRect(
            dataOrigin,
            ImVec2(dataOrigin.x + dataBoxWidth, dataOrigin.y + dataBoxHeight),
            IM_COL32(120, 200, 140, 210),
            8.0f,
            0,
            1.0f);

        auto tryGetFloat = [&dataContext](const char* key, float fallback = 0.0f) {
            if (const UIValue* value = dataContext.GetValue(key)) {
                if (const auto* floatValue = std::get_if<float>(&value->data)) {
                    return *floatValue;
                }
                if (const auto* intValue = std::get_if<int>(&value->data)) {
                    return static_cast<float>(*intValue);
                }
            }
            return fallback;
        };
        auto tryGetInt = [&dataContext](const char* key, int fallback = 0) {
            if (const UIValue* value = dataContext.GetValue(key)) {
                if (const auto* intValue = std::get_if<int>(&value->data)) {
                    return *intValue;
                }
                if (const auto* floatValue = std::get_if<float>(&value->data)) {
                    return static_cast<int>(*floatValue);
                }
            }
            return fallback;
        };
        auto tryGetBool = [&dataContext](const char* key, bool fallback = false) {
            if (const UIValue* value = dataContext.GetValue(key)) {
                if (const auto* boolValue = std::get_if<bool>(&value->data)) {
                    return *boolValue;
                }
            }
            return fallback;
        };
        auto tryGetString = [&dataContext](const char* key, std::string fallback = {}) {
            if (const UIValue* value = dataContext.GetValue(key)) {
                if (const auto* stringValue = std::get_if<std::string>(&value->data)) {
                    return *stringValue;
                }
            }
            return fallback;
        };

        ImVec2 dataCursor(dataOrigin.x + 10.0f, dataOrigin.y + 6.0f);
        drawList->AddText(dataCursor, IM_COL32(230, 255, 235, 255), dataPanelTitle);
        dataCursor.y += dataLineHeight;
        const std::array<std::string, 8> debugLines = {
            std::format("speed: {:.1f} km/h", tryGetFloat("bike.speedKmh")),
            
            std::format("energy: {:.2f}", tryGetFloat("bike.energy")),
            std::format("stamina: {:.2f}", tryGetFloat("bike.stamina")),
            std::format("distance: {:.1f} m", tryGetFloat("bike.distance")),
            std::format("lap: {}", tryGetString("race.lapTime")),
            std::format("current lap: {}", tryGetInt("race.currentLap", 1)),
            std::format("low energy: {}", tryGetBool("bike.isLowEnergy") ? "true" : "false")
        };
        for (const std::string& line : debugLines) {
            drawList->AddText(dataCursor, IM_COL32(220, 225, 235, 255), line.c_str());
            dataCursor.y += dataLineHeight;
        }
    }

    drawList->PopClipRect();
}

inline bool RenderSystem::ShouldShowRuntimeUiDebugPanel() const {
    return mState && mState->showEngineUi && mState->showDebugPanel && mRuntimeUiManager && mState->showRuntimeUi;
}

inline bool RenderSystem::ShouldDrawRuntimeUiDebugOverlay() const {
    return ShouldShowRuntimeUiDebugPanel() &&
        (mRuntimeUiDebugShowBounds || mRuntimeUiDebugShowHitRects || mRuntimeUiDebugShowBindingValues || mRuntimeUiDebugSelectedElementId != 0);
}

inline std::string RenderSystem::FormatUiValueForDebug(const UIValue* value) {
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
        return std::format("({:.2f}, {:.2f}, {:.2f}, {:.2f})", colorValue->x, colorValue->y, colorValue->z, colorValue->w);
    }
    return "<none>";
}

inline bool RenderSystem::IsRuntimeUiElementInteractive(const UIElement& element) {
    return element.visible && element.enabled && element.interactable &&
        (element.GetType() == UIElementType::Button ||
            element.GetType() == UIElementType::Toggle ||
            element.GetType() == UIElementType::Slider ||
            element.GetType() == UIElementType::InputField);
}

inline void RenderSystem::CollectRuntimeUiDebugRects(
    const UIScreen& screen,
    const UIElement& element,
    const UIRect& parentRect,
    std::vector<RuntimeUiDebugRect>& outRects) const
{
    if (element.GetType() != UIElementType::Canvas && !element.visible) {
        return;
    }

    const UIRect elementRect = element.GetType() == UIElementType::Canvas
        ? parentRect
        : element.transform.ComputeRect(parentRect);

    if (element.GetType() != UIElementType::Canvas) {
        outRects.push_back(RuntimeUiDebugRect{
            &screen,
            &element,
            elementRect,
            IsRuntimeUiElementInteractive(element)
        });
    }

    for (const auto& child : element.GetChildren()) {
        CollectRuntimeUiDebugRects(screen, *child, elementRect, outRects);
    }
}

inline void RenderSystem::DrawRuntimeUiDebugOverlay() {
    if (!ShouldDrawRuntimeUiDebugOverlay()) {
        return;
    }

    UIRenderContext context{};
    ImVec2 canvasMin{};
    ImVec2 canvasMax{};
    if (!BuildRuntimeUiRenderContext(context, canvasMin, canvasMax)) {
        return;
    }

    ImDrawList* drawList = static_cast<ImDrawList*>(context.nativeContext);
    if (!drawList) {
        return;
    }

    drawList->PushClipRect(canvasMin, canvasMax, true);
    const UIDataContext& dataContext = mRuntimeUiManager->GetDataContext();

    for (const auto& loadedScreen : mRuntimeUiManager->GetLoadedScreens()) {
        if (!loadedScreen.screen || !loadedScreen.screen->IsVisible() || !loadedScreen.screen->GetRootCanvas()) {
            continue;
        }

        std::vector<RuntimeUiDebugRect> rects;
        rects.reserve(64);
        const UIRect rootRect{ glm::vec2(0.0f), loadedScreen.screen->GetReferenceResolution(), 0.0f };
        CollectRuntimeUiDebugRects(*loadedScreen.screen, *loadedScreen.screen->GetRootCanvas(), rootRect, rects);

        for (const RuntimeUiDebugRect& entry : rects) {
            const ImVec2 min(
                context.rootPosition.x + entry.rect.position.x * context.axisScale.x,
                context.rootPosition.y + entry.rect.position.y * context.axisScale.y);
            const ImVec2 max(
                context.rootPosition.x + (entry.rect.position.x + entry.rect.size.x) * context.axisScale.x,
                context.rootPosition.y + (entry.rect.position.y + entry.rect.size.y) * context.axisScale.y);

            if (mRuntimeUiDebugShowBounds) {
                drawList->AddRect(min, max, IM_COL32(80, 220, 140, 210), 0.0f, 0, 1.0f);
            }
            if (mRuntimeUiDebugShowHitRects && entry.interactive) {
                drawList->AddRect(min, max, IM_COL32(255, 96, 96, 220), 0.0f, 0, 2.0f);
            }
            if (mRuntimeUiDebugSelectedElementId != 0 && entry.element && entry.element->GetId() == mRuntimeUiDebugSelectedElementId) {
                drawList->AddRect(min, max, IM_COL32(255, 215, 96, 255), 0.0f, 0, 2.5f);
            }
        }

        if (mRuntimeUiDebugShowBindingValues &&
            !mRuntimeUiDebugSelectedScreenName.empty() &&
            loadedScreen.screen->GetName() == mRuntimeUiDebugSelectedScreenName &&
            mRuntimeUiDebugSelectedElementId != 0) {
            const UIElement* selectedElement = loadedScreen.screen->FindById(mRuntimeUiDebugSelectedElementId);
            if (!selectedElement) {
                continue;
            }

            for (const RuntimeUiDebugRect& entry : rects) {
                if (!entry.element || entry.element->GetId() != mRuntimeUiDebugSelectedElementId) {
                    continue;
                }

                ImVec2 debugTextPos(
                    context.rootPosition.x + entry.rect.position.x * context.axisScale.x + 6.0f,
                    context.rootPosition.y + entry.rect.position.y * context.axisScale.y - 18.0f);
                for (const UIPropertyBinding& binding : loadedScreen.screen->GetBindings()) {
                    if (binding.targetElementId != mRuntimeUiDebugSelectedElementId) {
                        continue;
                    }
                    const std::string line = std::format(
                        "{} = {}",
                        binding.sourceKey,
                        FormatUiValueForDebug(dataContext.GetValue(binding.sourceKey)));
                    drawList->AddText(debugTextPos, IM_COL32(255, 245, 200, 255), line.c_str());
                    debugTextPos.y -= ImGui::GetTextLineHeightWithSpacing();
                }
                break;
            }
        }
    }

    drawList->PopClipRect();
}

inline bool RenderSystem::HandleRuntimeUiDebugSelection() {
    if (!ShouldShowRuntimeUiDebugPanel() ||
        !mRuntimeUiDebugEnablePicking ||
        !ImGui::IsMouseClicked(ImGuiMouseButton_Left) ||
        render_system_ui_editor::WantsMouseCapture() ||
        ImGuizmo::IsOver()) {
        return false;
    }

    UIRenderContext context{};
    ImVec2 canvasMin{};
    ImVec2 canvasMax{};
    if (!BuildRuntimeUiRenderContext(context, canvasMin, canvasMax)) {
        return false;
    }

    const ImVec2 mousePosition = ImGui::GetMousePos();
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
    const UIElement* hitElement = mRuntimeUiManager->DebugHitTestElement(runtimeMouse, &screenName, &elementId);
    if (!hitElement) {
        mRuntimeUiDebugSelectedElementId = 0;
        mRuntimeUiDebugSelectedScreenName.clear();
        return false;
    }

    mRuntimeUiDebugSelectedElementId = elementId;
    mRuntimeUiDebugSelectedScreenName = screenName;
    return true;
}

inline void RenderSystem::DrawRuntimeUiDebugPanel() {
    if (!ShouldShowRuntimeUiDebugPanel()) {
        return;
    }

    if (mRuntimeUiDebugSelectedElementId != 0 && !mRuntimeUiDebugSelectedScreenName.empty()) {
        const UIScreen* selectedScreen = mRuntimeUiManager->GetScreen(mRuntimeUiDebugSelectedScreenName);
        if (!selectedScreen || !selectedScreen->IsVisible() || !selectedScreen->FindById(mRuntimeUiDebugSelectedElementId)) {
            mRuntimeUiDebugSelectedElementId = 0;
            mRuntimeUiDebugSelectedScreenName.clear();
        }
    }

    UIManager::UISettings& settings = mRuntimeUiManager->GetSettings();
    const UIDataContext& dataContext = mRuntimeUiManager->GetDataContext();

    ImGui::SetNextWindowPos(ImVec2(1120, 140), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(420, 560), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Runtime UI Debug", nullptr)) {
        ImGui::End();
        return;
    }

    ImGui::TextUnformatted("Active Runtime UI Screens");
    for (const auto& loadedScreen : mRuntimeUiManager->GetLoadedScreens()) {
        if (!loadedScreen.screen) {
            continue;
        }
        ImGui::BulletText(
            "%s [%s] order=%d",
            loadedScreen.screen->GetName().c_str(),
            loadedScreen.screen->IsVisible() ? "Visible" : "Hidden",
            loadedScreen.screen->GetRenderOrder());
    }

    ImGui::Separator();
    ImGui::TextUnformatted("Runtime UI Debug Toggles");
    ImGui::Checkbox("Pick Runtime UI In Viewport", &mRuntimeUiDebugEnablePicking);
    ImGui::Checkbox("Show Live Runtime UI", &mRuntimeUiDebugShowLivePreview);
    ImGui::Checkbox("Show Runtime UI Stack", &mRuntimeUiDebugShowStack);
    ImGui::Checkbox("Show Runtime UI Bounds", &mRuntimeUiDebugShowBounds);
    ImGui::Checkbox("Show Hit Test Rects", &mRuntimeUiDebugShowHitRects);
    ImGui::Checkbox("Show Binding Debug Values", &mRuntimeUiDebugShowBindingValues);
    ImGui::Checkbox("Show Animation Debug", &mRuntimeUiDebugShowAnimationDebug);

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

    ImGui::Separator();
    ImGui::TextUnformatted("Selected Runtime UI Element");
    if (mRuntimeUiDebugSelectedElementId == 0 || mRuntimeUiDebugSelectedScreenName.empty()) {
        ImGui::TextDisabled("Click a runtime UI element in the viewport to inspect it.");
    }
    else {
        const UIScreen* selectedScreen = mRuntimeUiManager->GetScreen(mRuntimeUiDebugSelectedScreenName);
        const UIElement* selectedElement = selectedScreen ? selectedScreen->FindById(mRuntimeUiDebugSelectedElementId) : nullptr;
        if (!selectedScreen || !selectedElement) {
            ImGui::TextDisabled("Selected runtime UI element is no longer available.");
        }
        else {
            ImGui::Text("Screen: %s", selectedScreen->GetName().c_str());
            ImGui::Text("Name: %s", selectedElement->GetName().c_str());
            ImGui::Text("ID: %llu", static_cast<unsigned long long>(selectedElement->GetId()));
            ImGui::Text("Type: %s", ToString(selectedElement->GetType()));
            ImGui::Text("Visible / Enabled / Interactable: %s / %s / %s",
                selectedElement->visible ? "true" : "false",
                selectedElement->enabled ? "true" : "false",
                selectedElement->interactable ? "true" : "false");
            ImGui::Text("Position: (%.1f, %.1f)", selectedElement->transform.position.x, selectedElement->transform.position.y);
            ImGui::Text("Size: (%.1f, %.1f)", selectedElement->transform.size.x, selectedElement->transform.size.y);
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
                ImGui::BulletText("%s <- %s", ToString(binding.targetProperty).data(), binding.sourceKey.c_str());
                ImGui::Text("Current Value: %s", FormatUiValueForDebug(dataContext.GetValue(binding.sourceKey)).c_str());
            }
            if (!hasBinding) {
                ImGui::TextDisabled("No bindings on selected element.");
            }

            ImGui::Separator();
            ImGui::TextUnformatted("Active Animations");
            if (const UIAnimator* animator = mRuntimeUiManager->GetAnimator(selectedScreen->GetName())) {
                if (!mRuntimeUiDebugShowAnimationDebug) {
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
