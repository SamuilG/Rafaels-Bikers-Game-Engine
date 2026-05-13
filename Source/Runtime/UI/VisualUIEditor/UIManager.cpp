#include "UIManager.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <format>
#include <sstream>
#include <utility>
#include <vector>

#include "../EngineUi.hpp"
#include "UITheme.hpp"

#ifdef None
#undef None
#endif

namespace engine {

    namespace {

        struct UiHitCandidate {
            UIElement* element = nullptr;
            UIElementId elementId = 0;
            int zOrder = 0;
            int drawOrder = 0;
        };

        bool ContainsPoint(const UIRect& rect, const glm::vec2& point) {
            const glm::vec2 min = rect.Min();
            const glm::vec2 max = rect.Max();
            return point.x >= min.x && point.x <= max.x &&
                point.y >= min.y && point.y <= max.y;
        }

        void CollectHitCandidates(
            UIElement& element,
            const UIRect& parentRect,
            const glm::vec2& point,
            int& drawOrder,
            std::vector<UiHitCandidate>& candidates)
        {
            if (element.GetType() != UIElementType::Canvas && !element.visible) {
                return;
            }

            UIRect elementRect = parentRect;
            if (element.GetType() != UIElementType::Canvas) {
                elementRect = element.transform.ComputeRect(parentRect);
                if (ContainsPoint(elementRect, point)) {
                    candidates.push_back(UiHitCandidate{
                        &element,
                        element.GetId(),
                        element.zOrder,
                        drawOrder
                    });
                }
                ++drawOrder;
            }

            for (const auto& child : element.GetChildren()) {
                CollectHitCandidates(*child, elementRect, point, drawOrder, candidates);
            }
        }

        UIElement* FindByIdInScreen(UIScreen& screen, UIElementId id) {
            return screen.FindById(id);
        }

        const UIElement* FindByIdInScreen(const UIScreen& screen, UIElementId id) {
            return screen.FindById(id);
        }

        float NormalizeWidgetValue(float value, float minValue, float maxValue) {
            const float range = maxValue - minValue;
            if (std::abs(range) <= 0.0001f) {
                return 0.0f;
            }

            return std::clamp((value - minValue) / range, 0.0f, 1.0f);
        }

        bool TryGetElementRectRecursive(
            const UIElement& current,
            const UIRect& parentRect,
            UIElementId targetId,
            UIRect& outParentRect,
            UIRect& outElementRect)
        {
            const UIRect currentRect =
                current.GetType() == UIElementType::Canvas
                ? parentRect
                : current.transform.ComputeRect(parentRect);

            if (current.GetId() == targetId) {
                outParentRect = parentRect;
                outElementRect = currentRect;
                return true;
            }

            for (const auto& child : current.GetChildren()) {
                if (TryGetElementRectRecursive(*child, currentRect, targetId, outParentRect, outElementRect)) {
                    return true;
                }
            }

            return false;
        }

        bool TryGetNumericValue(const UIValue& value, float& outValue) {
            if (const auto* boolValue = std::get_if<bool>(&value.data)) {
                outValue = *boolValue ? 1.0f : 0.0f;
                return true;
            }
            if (const auto* intValue = std::get_if<int>(&value.data)) {
                outValue = static_cast<float>(*intValue);
                return true;
            }
            if (const auto* floatValue = std::get_if<float>(&value.data)) {
                outValue = *floatValue;
                return true;
            }
            return false;
        }

        bool TryGetBoolValue(const UIValue& value, bool& outValue) {
            if (const auto* boolValue = std::get_if<bool>(&value.data)) {
                outValue = *boolValue;
                return true;
            }
            if (const auto* intValue = std::get_if<int>(&value.data)) {
                outValue = *intValue != 0;
                return true;
            }
            if (const auto* floatValue = std::get_if<float>(&value.data)) {
                outValue = std::abs(*floatValue) > 0.0001f;
                return true;
            }
            if (const auto* stringValue = std::get_if<std::string>(&value.data)) {
                outValue = !stringValue->empty();
                return true;
            }
            return false;
        }

        bool TryGetVec2Value(const UIValue& value, glm::vec2& outValue) {
            if (const auto* vec2Value = std::get_if<glm::vec2>(&value.data)) {
                outValue = *vec2Value;
                return true;
            }
            return false;
        }

        bool TryGetColorValue(const UIValue& value, glm::vec4& outValue) {
            if (const auto* colorValue = std::get_if<glm::vec4>(&value.data)) {
                outValue = *colorValue;
                return true;
            }
            return false;
        }

        float ApplyNumericBindingModifiers(float value, const UIPropertyBinding& binding) {
            float result = value;
            if (binding.hasMin) {
                result = std::max(binding.minValue, result);
            }
            if (binding.hasMax) {
                result = std::min(binding.maxValue, result);
            }

            if (binding.invert) {
                if (binding.hasMin && binding.hasMax) {
                    result = binding.maxValue - (result - binding.minValue);
                }
                else {
                    result = -result;
                }
            }

            return result;
        }

        bool ApplyBooleanBindingModifiers(bool value, const UIPropertyBinding& binding) {
            return binding.invert ? !value : value;
        }

        int ParseFormatPrecision(std::string_view formatString) {
            const std::size_t precisionPrefix = formatString.find("{0:.");
            if (precisionPrefix == std::string_view::npos) {
                return -1;
            }

            const std::size_t numberStart = precisionPrefix + 4;
            std::size_t numberEnd = numberStart;
            while (numberEnd < formatString.size() && std::isdigit(static_cast<unsigned char>(formatString[numberEnd]))) {
                ++numberEnd;
            }

            if (numberEnd == numberStart || numberEnd >= formatString.size() || formatString[numberEnd] != 'f') {
                return -1;
            }

            try {
                return std::stoi(std::string(formatString.substr(numberStart, numberEnd - numberStart)));
            }
            catch (...) {
                return -1;
            }
        }

        std::string FormatNumericText(float value, const UIPropertyBinding& binding) {
            if (binding.formatString.empty()) {
                return std::format("{}", value);
            }

            const std::size_t tokenStart = binding.formatString.find("{0");
            const std::size_t tokenEnd = binding.formatString.find('}', tokenStart);
            if (tokenStart == std::string::npos || tokenEnd == std::string::npos) {
                return binding.formatString;
            }

            const int precision = ParseFormatPrecision(binding.formatString);
            std::string valueText;
            if (precision >= 0) {
                valueText = std::format("{:.{}f}", value, precision);
            }
            else {
                valueText = std::format("{}", value);
            }

            std::string formatted = binding.formatString;
            formatted.replace(tokenStart, tokenEnd - tokenStart + 1, valueText);
            return formatted;
        }

        std::string FormatBindingText(const UIValue& value, const UIPropertyBinding& binding) {
            if (const auto* stringValue = std::get_if<std::string>(&value.data)) {
                if (binding.formatString.empty()) {
                    return *stringValue;
                }
                std::string formatted = binding.formatString;
                const std::size_t tokenStart = formatted.find("{0}");
                if (tokenStart != std::string::npos) {
                    formatted.replace(tokenStart, 3, *stringValue);
                    return formatted;
                }
                return formatted;
            }

            if (const auto* vec2Value = std::get_if<glm::vec2>(&value.data)) {
                return std::format("{:.2f}, {:.2f}", vec2Value->x, vec2Value->y);
            }

            if (const auto* colorValue = std::get_if<glm::vec4>(&value.data)) {
                return std::format("{:.2f}, {:.2f}, {:.2f}, {:.2f}", colorValue->x, colorValue->y, colorValue->z, colorValue->w);
            }

            float numericValue = 0.0f;
            if (TryGetNumericValue(value, numericValue)) {
                return FormatNumericText(ApplyNumericBindingModifiers(numericValue, binding), binding);
            }

            return {};
        }

        void LogBindingWarningOnce(bool& warnedFlag, const std::string& message) {
            if (warnedFlag) {
                return;
            }

            warnedFlag = true;
            EngineUi::LogPrint("{}\n", message);
        }

        float ComputeTransitionAlpha(float deltaTime, float duration) {
            if (duration <= 0.0001f) {
                return 1.0f;
            }

            return std::clamp(deltaTime / duration, 0.0f, 1.0f);
        }

        glm::vec4 LerpColor(const glm::vec4& from, const glm::vec4& to, float alpha) {
            return glm::mix(from, to, std::clamp(alpha, 0.0f, 1.0f));
        }

    } // namespace

    std::filesystem::path UIManager::NormalizePath(const std::filesystem::path& path) {
        if (path.empty()) {
            return {};
        }

        std::filesystem::path normalized = path;
        if (!normalized.has_parent_path()) {
            normalized = std::filesystem::path("Assets") / "ui" / normalized;
        }
        else {
            std::string parent = normalized.parent_path().generic_string();
            std::transform(parent.begin(), parent.end(), parent.begin(), [](unsigned char ch) {
                return static_cast<char>(std::tolower(ch));
            });
            if (parent == "assets/ui") {
                normalized = std::filesystem::path("Assets") / "ui" / normalized.filename();
            }
        }

        const std::string filename = normalized.filename().string();
        if (!filename.ends_with(".ui.json")) {
            if (normalized.extension() == ".json") {
                normalized.replace_extension();
            }
            normalized += ".ui.json";
        }

        return normalized;
    }

    UIManager::LoadedScreen* UIManager::FindLoadedScreen(std::string_view name) {
        const auto iterator = std::find_if(mLoadedScreens.begin(), mLoadedScreens.end(), [&](const LoadedScreen& loadedScreen) {
            return loadedScreen.screen && loadedScreen.screen->GetName() == name;
        });
        return iterator == mLoadedScreens.end() ? nullptr : &(*iterator);
    }

    const UIManager::LoadedScreen* UIManager::FindLoadedScreen(std::string_view name) const {
        const auto iterator = std::find_if(mLoadedScreens.begin(), mLoadedScreens.end(), [&](const LoadedScreen& loadedScreen) {
            return loadedScreen.screen && loadedScreen.screen->GetName() == name;
        });
        return iterator == mLoadedScreens.end() ? nullptr : &(*iterator);
    }

    UIManager::LoadedScreen* UIManager::FindLoadedScreenByPath(const std::filesystem::path& path) {
        const auto iterator = std::find_if(mLoadedScreens.begin(), mLoadedScreens.end(), [&](const LoadedScreen& loadedScreen) {
            return loadedScreen.path == path;
        });
        return iterator == mLoadedScreens.end() ? nullptr : &(*iterator);
    }

    const UIManager::LoadedScreen* UIManager::FindLoadedScreenByPath(const std::filesystem::path& path) const {
        const auto iterator = std::find_if(mLoadedScreens.begin(), mLoadedScreens.end(), [&](const LoadedScreen& loadedScreen) {
            return loadedScreen.path == path;
        });
        return iterator == mLoadedScreens.end() ? nullptr : &(*iterator);
    }

    void UIManager::SyncScreenRenderOrder() {
        for (std::size_t index = 0; index < mLoadedScreens.size(); ++index) {
            if (mLoadedScreens[index].screen) {
                mLoadedScreens[index].screen->SetRenderOrder(static_cast<int>(index));
            }
        }
    }

    void UIManager::RefreshActiveScreenCache() {
        mActiveScreen = nullptr;
        mActiveScreenPath.clear();

        for (auto it = mLoadedScreens.rbegin(); it != mLoadedScreens.rend(); ++it) {
            if (!it->screen || !it->screen->IsVisible()) {
                continue;
            }

            mActiveScreen = it->screen.get();
            mActiveScreenPath = it->path;
            return;
        }
    }

    void UIManager::ClearPressedStates() {
        for (auto& loadedScreen : mLoadedScreens) {
            if (!loadedScreen.screen || !loadedScreen.screen->GetRootCanvas()) {
                continue;
            }

            loadedScreen.screen->GetRootCanvas()->Traverse([&](UIElement& element) {
                if (auto* button = dynamic_cast<UIButton*>(&element)) {
                    button->pressed = false;
                }
            });
        }
    }

    void UIManager::ClearInputStateForScreen(const UIScreen* screen) {
        if (!screen) {
            return;
        }

        if (mHoveredScreen == screen) {
            mHoveredScreen = nullptr;
            mHoveredElementId = 0;
        }
        if (mPressedScreen == screen) {
            mPressedScreen = nullptr;
            mPressedElementId = 0;
        }
        if (mFocusedScreen == screen) {
            mFocusedScreen = nullptr;
            mFocusedElementId = 0;
        }
    }

    void UIManager::SyncAnimator(LoadedScreen& loadedScreen) {
        loadedScreen.animator.SetScreen(loadedScreen.screen.get());
        loadedScreen.activeEnterAnimation.clear();
        loadedScreen.activeExitAnimation.clear();
        loadedScreen.pendingHideAfterExit = false;
    }

    bool UIManager::BeginScreenEnterTransition(LoadedScreen& loadedScreen) {
        if (!loadedScreen.screen) {
            return false;
        }

        loadedScreen.pendingHideAfterExit = false;
        loadedScreen.activeExitAnimation.clear();

        const std::string& enterAnimation = loadedScreen.screen->GetEnterAnimation();
        if (enterAnimation.empty()) {
            loadedScreen.activeEnterAnimation.clear();
            return false;
        }

        if (loadedScreen.animator.Play(enterAnimation)) {
            loadedScreen.activeEnterAnimation = enterAnimation;
            return true;
        }

        loadedScreen.activeEnterAnimation.clear();
        return false;
    }

    bool UIManager::BeginScreenExitTransition(LoadedScreen& loadedScreen) {
        if (!loadedScreen.screen) {
            return false;
        }

        const std::string& exitAnimation = loadedScreen.screen->GetExitAnimation();
        if (exitAnimation.empty()) {
            loadedScreen.activeExitAnimation.clear();
            loadedScreen.pendingHideAfterExit = false;
            return false;
        }

        loadedScreen.animator.StopAll();
        if (loadedScreen.animator.Play(exitAnimation)) {
            loadedScreen.activeEnterAnimation.clear();
            loadedScreen.activeExitAnimation = exitAnimation;
            loadedScreen.pendingHideAfterExit = loadedScreen.screen->HideAfterExit();
            return true;
        }

        loadedScreen.activeExitAnimation.clear();
        loadedScreen.pendingHideAfterExit = false;
        return false;
    }

    bool UIManager::IsScreenTransitionBlockingInput(const LoadedScreen& loadedScreen) const {
        if (!loadedScreen.screen || !loadedScreen.screen->BlockInputDuringTransition()) {
            return false;
        }

        const bool enterActive =
            !loadedScreen.activeEnterAnimation.empty() &&
            loadedScreen.animator.IsPlaying(loadedScreen.activeEnterAnimation);
        const bool exitActive =
            !loadedScreen.activeExitAnimation.empty() &&
            loadedScreen.animator.IsPlaying(loadedScreen.activeExitAnimation);
        return enterActive || exitActive || loadedScreen.pendingHideAfterExit;
    }

    void UIManager::TryAutoPlayScreenAnimation(LoadedScreen& loadedScreen) {
        if (!loadedScreen.screen || !loadedScreen.screen->IsVisible()) {
            return;
        }

        bool startedConfiguredClip = false;
        startedConfiguredClip |= BeginScreenEnterTransition(loadedScreen);
        for (const UIAnimationClip& clip : loadedScreen.screen->GetAnimationClips()) {
            if (!clip.playOnShow || clip.tracks.empty()) {
                continue;
            }
            if (!loadedScreen.screen->GetEnterAnimation().empty() && clip.name == loadedScreen.screen->GetEnterAnimation()) {
                continue;
            }
            startedConfiguredClip |= loadedScreen.animator.Play(clip.name);
        }

        if (startedConfiguredClip) {
            return;
        }

        const std::string& screenName = loadedScreen.screen->GetName();
        const std::array<std::string, 2> preferredClips = {
            std::format("{}_FadeIn", screenName),
            std::format("{}_SlideIn", screenName)
        };

        for (const std::string& clipName : preferredClips) {
            const auto clipIt = std::find_if(
                loadedScreen.screen->GetAnimationClips().begin(),
                loadedScreen.screen->GetAnimationClips().end(),
                [&](const UIAnimationClip& clip) {
                    return clip.name == clipName;
                });
            if (clipIt != loadedScreen.screen->GetAnimationClips().end() && loadedScreen.animator.Play(clipName)) {
                break;
            }
        }

        if (!loadedScreen.animator.GetActiveClips().empty()) {
            return;
        }

        const auto fallbackClip = std::find_if(
            loadedScreen.screen->GetAnimationClips().begin(),
            loadedScreen.screen->GetAnimationClips().end(),
            [&](const UIAnimationClip& clip) {
                return !clip.tracks.empty() && clip.name != "LowEnergy_Pulse";
            });

        if (fallbackClip != loadedScreen.screen->GetAnimationClips().end()) {
            loadedScreen.animator.Play(fallbackClip->name);
        }
    }

    void UIManager::RestartVisibleScreenAnimations() {
        for (LoadedScreen& loadedScreen : mLoadedScreens) {
            if (!loadedScreen.screen || !loadedScreen.screen->IsVisible()) {
                continue;
            }

            loadedScreen.animator.StopAll();
            TryAutoPlayScreenAnimation(loadedScreen);
        }
    }

    void UIManager::UpdateConditionalAnimations(LoadedScreen& loadedScreen) {
        if (!loadedScreen.screen || !loadedScreen.screen->IsVisible()) {
            loadedScreen.animator.Stop("LowEnergy_Pulse");
            return;
        }

        if (loadedScreen.screen->GetName() != "HUD") {
            return;
        }

        const UIElement* lowEnergyWarning = nullptr;
        if (const auto& clips = loadedScreen.screen->GetAnimationClips(); !clips.empty()) {
            for (const UIAnimationClip& clip : clips) {
                if (clip.name != "LowEnergy_Pulse" || clip.tracks.empty()) {
                    continue;
                }
                lowEnergyWarning = loadedScreen.screen->FindById(clip.tracks.front().targetElementId);
                break;
            }
        }

        if (!lowEnergyWarning) {
            return;
        }

        if (lowEnergyWarning->visible) {
            if (!loadedScreen.animator.IsPlaying("LowEnergy_Pulse")) {
                loadedScreen.animator.Play("LowEnergy_Pulse");
            }
        }
        else {
            loadedScreen.animator.Stop("LowEnergy_Pulse");
        }
    }

    void UIManager::ApplyBindings(UIScreen& screen) {
        for (UIPropertyBinding& binding : screen.GetBindings()) {
            ApplyBinding(screen, binding);
        }
    }

    void UIManager::ApplyBinding(UIScreen& screen, UIPropertyBinding& binding) {
        if (binding.targetElementId == 0 || binding.sourceKey.empty()) {
            return;
        }

        const UIValue* sourceValue = mDataContext.GetValue(binding.sourceKey);
        if (!sourceValue) {
            LogBindingWarningOnce(
                binding.warnedMissingSource,
                std::format("[UIBinding][Warning] Missing source '{}' for screen '{}' element {}",
                    binding.sourceKey,
                    screen.GetName(),
                    binding.targetElementId));
            return;
        }

        binding.warnedMissingSource = false;

        const std::uint64_t revision = mDataContext.GetRevision(binding.sourceKey);
        if (binding.updateMode == UIBindingUpdateMode::OnChange &&
            binding.lastObservedRevision == revision &&
            binding.lastAppliedValue.has_value()) {
            return;
        }

        UIElement* target = screen.FindById(binding.targetElementId);
        if (!target) {
            LogBindingWarningOnce(
                binding.warnedInvalidTarget,
                std::format("[UIBinding][Warning] Missing target element {} for screen '{}'",
                    binding.targetElementId,
                    screen.GetName()));
            return;
        }

        binding.warnedInvalidTarget = false;

        if (!target->runtimeMutable) {
            return;
        }

        auto typeMismatch = [&]() {
            LogBindingWarningOnce(
                binding.warnedTypeMismatch,
                std::format("[UIBinding][Warning] Type mismatch for '{}' -> {} on '{}'",
                    binding.sourceKey,
                    ToString(binding.targetProperty),
                    target->GetName()));
        };

        bool applied = false;
        switch (binding.targetProperty) {
        case UIBindingTargetProperty::TextText:
            if (auto* text = dynamic_cast<UIText*>(target)) {
                text->text = FormatBindingText(*sourceValue, binding);
                applied = true;
            }
            else {
                typeMismatch();
            }
            break;

        case UIBindingTargetProperty::ProgressBarValue: {
            if (auto* progressBar = dynamic_cast<UIProgressBar*>(target)) {
                float numericValue = 0.0f;
                if (TryGetNumericValue(*sourceValue, numericValue)) {
                    progressBar->value = ApplyNumericBindingModifiers(numericValue, binding);
                    progressBar->value = std::clamp(progressBar->value, progressBar->minValue, progressBar->maxValue);
                    applied = true;
                }
                else {
                    typeMismatch();
                }
            }
            else {
                typeMismatch();
            }
            break;
        }

        case UIBindingTargetProperty::SliderValue: {
            if (auto* slider = dynamic_cast<UISlider*>(target)) {
                float numericValue = 0.0f;
                if (TryGetNumericValue(*sourceValue, numericValue)) {
                    slider->value = ApplyNumericBindingModifiers(numericValue, binding);
                    slider->value = std::clamp(slider->value, slider->minValue, slider->maxValue);
                    if (slider->wholeNumbers) {
                        slider->value = std::round(slider->value);
                    }
                    applied = true;
                }
                else {
                    typeMismatch();
                }
            }
            else {
                typeMismatch();
            }
            break;
        }

        case UIBindingTargetProperty::ImageTintColor: {
            glm::vec4 colorValue(1.0f);
            if (TryGetColorValue(*sourceValue, colorValue)) {
                target->style.tintColor = colorValue;
                applied = true;
            }
            else {
                typeMismatch();
            }
            break;
        }

        case UIBindingTargetProperty::ElementVisible: {
            bool boolValue = false;
            if (TryGetBoolValue(*sourceValue, boolValue)) {
                target->visible = ApplyBooleanBindingModifiers(boolValue, binding);
                applied = true;
            }
            else {
                typeMismatch();
            }
            break;
        }

        case UIBindingTargetProperty::ElementOpacity: {
            float numericValue = 0.0f;
            if (TryGetNumericValue(*sourceValue, numericValue)) {
                target->style.opacity = std::clamp(ApplyNumericBindingModifiers(numericValue, binding), 0.0f, 1.0f);
                applied = true;
            }
            else {
                typeMismatch();
            }
            break;
        }

        case UIBindingTargetProperty::ElementPosition: {
            glm::vec2 vec2Value(0.0f);
            if (TryGetVec2Value(*sourceValue, vec2Value)) {
                target->transform.position = vec2Value;
                applied = true;
            }
            else {
                typeMismatch();
            }
            break;
        }

        case UIBindingTargetProperty::ElementRotation: {
            float numericValue = 0.0f;
            if (TryGetNumericValue(*sourceValue, numericValue)) {
                target->transform.rotation = ApplyNumericBindingModifiers(numericValue, binding);
                applied = true;
            }
            else {
                typeMismatch();
            }
            break;
        }
        }

        if (applied) {
            binding.warnedTypeMismatch = false;
            binding.lastObservedRevision = revision;
            binding.lastAppliedValue = *sourceValue;
        }
    }

    bool UIManager::LoadScreen(const std::filesystem::path& path) {
        const std::filesystem::path normalizedPath = NormalizePath(path);
        if (normalizedPath.empty()) {
            return false;
        }

        auto loaded = UISerializer::LoadFromFile(normalizedPath);
        if (!loaded) {
            EngineUi::LogPrint("[UIManager] Failed to load screen from {}\n", normalizedPath.generic_string());
            return false;
        }

        return LoadScreen(loaded->GetName(), normalizedPath);
    }

    bool UIManager::LoadScreen(const std::string& name, const std::filesystem::path& path) {
        const std::filesystem::path normalizedPath = NormalizePath(path);
        if (normalizedPath.empty()) {
            return false;
        }

        auto loaded = UISerializer::LoadFromFile(normalizedPath);
        if (!loaded) {
            EngineUi::LogPrint("[UIManager] Failed to load screen from {}\n", normalizedPath.generic_string());
            return false;
        }

        if (!name.empty()) {
            loaded->SetName(name);
        }

        if (LoadedScreen* existingByName = FindLoadedScreen(loaded->GetName())) {
            const bool preserveVisible = existingByName->screen && existingByName->screen->IsVisible();
            const int preserveOrder = existingByName->screen ? existingByName->screen->GetRenderOrder() : 0;
            loaded->SetVisible(preserveVisible);
            loaded->SetRenderOrder(preserveOrder);
            existingByName->screen = std::move(loaded);
            existingByName->path = normalizedPath;
            SyncAnimator(*existingByName);
            if (existingByName->screen->IsVisible()) {
                TryAutoPlayScreenAnimation(*existingByName);
            }
            RefreshActiveScreenCache();
            ClearPressedStates();
            EngineUi::LogPrint("[UIManager] Replaced screen '{}'\n", existingByName->screen->GetName());
            return true;
        }

        mLoadedScreens.push_back(LoadedScreen{ std::move(loaded), normalizedPath, {} });
        SyncAnimator(mLoadedScreens.back());
        if (mLoadedScreens.back().screen && mLoadedScreens.back().screen->IsVisible()) {
            TryAutoPlayScreenAnimation(mLoadedScreens.back());
        }
        SyncScreenRenderOrder();
        RefreshActiveScreenCache();
        mHoveredScreen = nullptr;
        mHoveredElementId = 0;
        mPressedScreen = nullptr;
        mPressedElementId = 0;
        mMousePosition = glm::vec2(0.0f);
        EngineUi::LogPrint("[UIManager] Loaded screen '{}'\n", normalizedPath.generic_string());
        return true;
    }

    bool UIManager::ReloadScreen(const std::filesystem::path& path) {
        const std::filesystem::path normalizedPath = NormalizePath(path);
        if (normalizedPath.empty()) {
            return false;
        }

        LoadedScreen* existing = FindLoadedScreenByPath(normalizedPath);
        if (!existing) {
            return false;
        }

        auto reloaded = UISerializer::LoadFromFile(normalizedPath);
        if (!reloaded) {
            EngineUi::LogPrint("[UIManager] Failed to reload screen from {}\n", normalizedPath.generic_string());
            return false;
        }

        const bool preserveVisible = existing->screen && existing->screen->IsVisible();
        const int preserveOrder = existing->screen ? existing->screen->GetRenderOrder() : 0;
        reloaded->SetVisible(preserveVisible);
        reloaded->SetRenderOrder(preserveOrder);
        existing->screen = std::move(reloaded);
        SyncAnimator(*existing);
        if (existing->screen->IsVisible()) {
            TryAutoPlayScreenAnimation(*existing);
        }

        SyncScreenRenderOrder();
        RefreshActiveScreenCache();
        mHoveredScreen = nullptr;
        mHoveredElementId = 0;
        mPressedScreen = nullptr;
        mPressedElementId = 0;
        ClearPressedStates();
        EngineUi::LogPrint("[UIManager] Reloaded screen '{}'\n", normalizedPath.generic_string());
        return true;
    }

    bool UIManager::ReplaceWithScreen(const std::filesystem::path& path) {
        ClearScreens();
        return LoadScreen(path);
    }

    bool UIManager::UnloadScreen(std::string_view name) {
        if (mLoadedScreens.empty()) {
            return false;
        }

        bool removedAny = false;
        if (name.empty()) {
            if (mLoadedScreens.back().screen) {
                ClearInputStateForScreen(mLoadedScreens.back().screen.get());
                EngineUi::LogPrint("[UIManager] Unloaded screen '{}'\n", mLoadedScreens.back().screen->GetName());
            }
            mLoadedScreens.pop_back();
            removedAny = true;
        }
        else {
            for (auto it = mLoadedScreens.begin(); it != mLoadedScreens.end();) {
                if (it->screen && it->screen->GetName() == name) {
                    ClearInputStateForScreen(it->screen.get());
                    EngineUi::LogPrint("[UIManager] Unloaded screen '{}'\n", it->screen->GetName());
                    it = mLoadedScreens.erase(it);
                    removedAny = true;
                }
                else {
                    ++it;
                }
            }
        }

        if (removedAny) {
            SyncScreenRenderOrder();
            RefreshActiveScreenCache();
            ClearPressedStates();
        }

        return removedAny;
    }

    void UIManager::ClearScreens() {
        if (mLoadedScreens.empty()) {
            return;
        }

        EngineUi::LogPrint("[UIManager] Cleared {} loaded screen(s)\n", mLoadedScreens.size());
        mLoadedScreens.clear();
        RefreshActiveScreenCache();
        mHoveredScreen = nullptr;
        mHoveredElementId = 0;
        mPressedScreen = nullptr;
        mPressedElementId = 0;
        mFocusedScreen = nullptr;
        mFocusedElementId = 0;
        mMousePosition = glm::vec2(0.0f);
    }

    void UIManager::ShowScreen(std::string_view name) {
        if (LoadedScreen* loadedScreen = FindLoadedScreen(name)) {
            loadedScreen->screen->SetVisible(true);
            TryAutoPlayScreenAnimation(*loadedScreen);
            RefreshActiveScreenCache();
        }
    }

    void UIManager::HideScreen(std::string_view name) {
        if (LoadedScreen* loadedScreen = FindLoadedScreen(name)) {
            const bool startedExit = BeginScreenExitTransition(*loadedScreen);
            if (!startedExit || !loadedScreen->screen->HideAfterExit()) {
                loadedScreen->screen->SetVisible(false);
                loadedScreen->animator.StopAll();
                loadedScreen->activeEnterAnimation.clear();
                loadedScreen->activeExitAnimation.clear();
                loadedScreen->pendingHideAfterExit = false;
                ClearInputStateForScreen(loadedScreen->screen.get());
                RefreshActiveScreenCache();
                ClearPressedStates();
            }
        }
    }

    void UIManager::ToggleScreen(std::string_view name) {
        if (LoadedScreen* loadedScreen = FindLoadedScreen(name)) {
            if (loadedScreen->screen->IsVisible()) {
                HideScreen(name);
            }
            else {
                ShowScreen(name);
            }
        }
    }

    void UIManager::SwitchToScreen(std::string_view name) {
        if (!FindLoadedScreen(name)) {
            return;
        }

        for (auto& loadedScreen : mLoadedScreens) {
            if (loadedScreen.screen && loadedScreen.screen->GetName() != name && loadedScreen.screen->IsVisible()) {
                HideScreen(loadedScreen.screen->GetName());
            }
        }

        ShowScreen(name);
        PushScreen(name);
    }

    void UIManager::PushScreen(std::string_view name) {
        auto iterator = std::find_if(mLoadedScreens.begin(), mLoadedScreens.end(), [&](const LoadedScreen& loadedScreen) {
            return loadedScreen.screen && loadedScreen.screen->GetName() == name;
        });
        if (iterator == mLoadedScreens.end()) {
            return;
        }

        iterator->screen->SetVisible(true);
        if (std::next(iterator) != mLoadedScreens.end()) {
            LoadedScreen promoted = std::move(*iterator);
            mLoadedScreens.erase(iterator);
            mLoadedScreens.push_back(std::move(promoted));
            TryAutoPlayScreenAnimation(mLoadedScreens.back());
        }
        else {
            TryAutoPlayScreenAnimation(*iterator);
        }

        SyncScreenRenderOrder();
        RefreshActiveScreenCache();
    }

    void UIManager::PopScreen() {
        for (auto it = mLoadedScreens.rbegin(); it != mLoadedScreens.rend(); ++it) {
            if (!it->screen || !it->screen->IsVisible()) {
                continue;
            }

            HideScreen(it->screen->GetName());
            return;
        }
    }

    void UIManager::Update(float deltaTime) {
        mLastDeltaTime = deltaTime;
        if (mLoadedScreens.empty()) {
            return;
        }

        for (auto& loadedScreen : mLoadedScreens) {
            if (!loadedScreen.screen || !loadedScreen.screen->IsVisible() || !loadedScreen.screen->GetRootCanvas()) {
                continue;
            }

            ApplyBindings(*loadedScreen.screen);
            UpdateConditionalAnimations(loadedScreen);
            loadedScreen.animator.Update(deltaTime * std::max(0.0f, mSettings.animationSpeed));

            if (!loadedScreen.activeEnterAnimation.empty() &&
                !loadedScreen.animator.IsPlaying(loadedScreen.activeEnterAnimation)) {
                loadedScreen.activeEnterAnimation.clear();
            }

            if (!loadedScreen.activeExitAnimation.empty() &&
                !loadedScreen.animator.IsPlaying(loadedScreen.activeExitAnimation)) {
                loadedScreen.activeExitAnimation.clear();
                if (loadedScreen.pendingHideAfterExit && loadedScreen.screen->HideAfterExit()) {
                    loadedScreen.pendingHideAfterExit = false;
                    loadedScreen.screen->SetVisible(false);
                    loadedScreen.animator.StopAll();
                    ClearInputStateForScreen(loadedScreen.screen.get());
                    RefreshActiveScreenCache();
                    ClearPressedStates();
                    continue;
                }
            }

            const auto theme = LoadUiTheme(loadedScreen.screen->GetThemePath());
            const UITheme* resolvedTheme = theme.get();

            loadedScreen.screen->GetRootCanvas()->Traverse([&](UIElement& element) {
                if (auto* button = dynamic_cast<UIButton*>(&element)) {
                    const bool isPressed = (loadedScreen.screen.get() == mPressedScreen) && (button->GetId() == mPressedElementId);
                    const bool isHovered = (loadedScreen.screen.get() == mHoveredScreen) && (button->GetId() == mHoveredElementId);
                    button->pressed = isPressed;

                    const ResolvedUIButtonStyle buttonStyle = ResolveButtonStyle(element, *button, resolvedTheme);
                    glm::vec4 targetColor = buttonStyle.normalColor;
                    float targetScale = buttonStyle.normalScale;
                    switch (buttonStyle.transitionMode) {
                    case UIButtonTransitionMode::None:
                        targetColor = (!element.enabled || !element.interactable) ? buttonStyle.disabledColor : buttonStyle.normalColor;
                        targetScale = buttonStyle.normalScale;
                        break;
                    case UIButtonTransitionMode::ColorTint:
                        if (!element.enabled || !element.interactable) {
                            targetColor = buttonStyle.disabledColor;
                        }
                        else if (isPressed) {
                            targetColor = buttonStyle.pressedColor;
                        }
                        else if (isHovered) {
                            targetColor = buttonStyle.hoverColor;
                        }
                        targetScale = buttonStyle.normalScale;
                        break;
                    case UIButtonTransitionMode::Scale:
                        targetColor = (!element.enabled || !element.interactable) ? buttonStyle.disabledColor : buttonStyle.normalColor;
                        if (!element.enabled || !element.interactable) {
                            targetScale = buttonStyle.normalScale;
                        }
                        else if (isPressed) {
                            targetScale = buttonStyle.pressedScale;
                        }
                        else if (isHovered) {
                            targetScale = buttonStyle.hoverScale;
                        }
                        break;
                    case UIButtonTransitionMode::Animation:
                    default:
                        if (!element.enabled || !element.interactable) {
                            targetColor = buttonStyle.disabledColor;
                            targetScale = buttonStyle.normalScale;
                        }
                        else if (isPressed) {
                            targetColor = buttonStyle.pressedColor;
                            targetScale = buttonStyle.pressedScale;
                        }
                        else if (isHovered) {
                            targetColor = buttonStyle.hoverColor;
                            targetScale = buttonStyle.hoverScale;
                        }
                        break;
                    }

                    if (!button->runtimeVisualInitialized) {
                        button->runtimeVisualColor = targetColor;
                        button->runtimeVisualScale = targetScale;
                        button->runtimeVisualInitialized = true;
                    }
                    else {
                        const float alpha = ComputeTransitionAlpha(deltaTime, std::max(0.01f, buttonStyle.transitionDuration));
                        button->runtimeVisualColor = LerpColor(button->runtimeVisualColor, targetColor, alpha);
                        button->runtimeVisualScale = glm::mix(button->runtimeVisualScale, targetScale, alpha);
                    }
                }
                else if (auto* slider = dynamic_cast<UISlider*>(&element)) {
                    if ((loadedScreen.screen.get() == mPressedScreen) && (slider->GetId() == mPressedElementId)) {
                        const UIRect rootRect{
                            glm::vec2(0.0f),
                            loadedScreen.screen->GetReferenceResolution(),
                            0.0f
                        };
                        UIRect parentRect{};
                        UIRect sliderRect{};
                        if (!TryGetElementRectRecursive(
                            *loadedScreen.screen->GetRootCanvas(),
                            rootRect,
                            slider->GetId(),
                            parentRect,
                            sliderRect)) {
                            return;
                        }

                        const float normalized = std::clamp((mMousePosition.x - sliderRect.position.x) / std::max(1.0f, sliderRect.size.x), 0.0f, 1.0f);
                        const float rawValue = slider->minValue + (slider->maxValue - slider->minValue) * normalized;
                        const float newValue = slider->wholeNumbers ? std::round(rawValue) : rawValue;
                        if (std::abs(slider->value - newValue) > 0.0001f) {
                            slider->value = newValue;
                            if (!slider->events.onValueChanged.empty()) {
                                TriggerEvent(slider->events.onValueChanged);
                            }
                        }
                    }
                }
            });
        }
    }

    void UIManager::Render(const UIRenderContext& context) {
        if (mLoadedScreens.empty() || !mRenderer) {
            return;
        }

        for (const auto& loadedScreen : mLoadedScreens) {
            if (!loadedScreen.screen || !loadedScreen.screen->IsVisible()) {
                continue;
            }

            ApplyBindings(*loadedScreen.screen);

            const UIElementId hoveredId = loadedScreen.screen.get() == mHoveredScreen ? mHoveredElementId : 0;
            const UIElementId pressedId = loadedScreen.screen.get() == mPressedScreen ? mPressedElementId : 0;
            mRenderer->RenderScreen(*loadedScreen.screen, context, hoveredId, pressedId);
        }
    }

    void UIManager::HandleMouseMove(const glm::vec2& position) {
        mMousePosition = position;
        const HitResult hitResult = HitTest(position);
        mHoveredScreen = hitResult.screen;
        mHoveredElementId = hitResult.elementId;
    }

    void UIManager::HandleMouseDown() {
        if (mLoadedScreens.empty()) {
            return;
        }

        const HitResult hitResult = HitTest(mMousePosition);
        if (hitResult.element && IsInteractive(*hitResult.element)) {
            mPressedScreen = hitResult.screen;
            mPressedElementId = hitResult.elementId;
            if (hitResult.element->GetType() == UIElementType::InputField) {
                mFocusedScreen = hitResult.screen;
                mFocusedElementId = hitResult.elementId;
            }
            else {
                mFocusedScreen = nullptr;
                mFocusedElementId = 0;
            }
            if (!hitResult.element->events.onPressed.empty()) {
                TriggerEvent(hitResult.element->events.onPressed);
            }
        }
        else {
            mPressedScreen = nullptr;
            mPressedElementId = 0;
            mFocusedScreen = nullptr;
            mFocusedElementId = 0;
        }
    }

    void UIManager::HandleMouseUp() {
        if (mLoadedScreens.empty()) {
            mPressedScreen = nullptr;
            mPressedElementId = 0;
            return;
        }

        UIScreen* releasedScreen = mPressedScreen;
        const UIElementId releasedId = mPressedElementId;
        const HitResult hitResult = HitTest(mMousePosition);
        mHoveredScreen = hitResult.screen;
        mHoveredElementId = hitResult.elementId;

        if (releasedScreen && releasedId != 0 &&
            releasedScreen == hitResult.screen &&
            hitResult.element &&
            releasedId == hitResult.elementId &&
            IsInteractive(*hitResult.element)) {
            if (auto* toggle = dynamic_cast<UIToggle*>(hitResult.element)) {
                toggle->isOn = !toggle->isOn;
                if (!toggle->events.onValueChanged.empty()) {
                    TriggerEvent(toggle->events.onValueChanged);
                }
            }
            if (!hitResult.element->events.onReleased.empty()) {
                TriggerEvent(hitResult.element->events.onReleased);
            }
            if (!hitResult.element->events.onClick.empty()) {
                TriggerEvent(hitResult.element->events.onClick);
            }
        }

        mPressedScreen = nullptr;
        mPressedElementId = 0;
    }

    void UIManager::HandleTextInput(const std::string& text) {
        if (text.empty() || !mFocusedScreen || mFocusedElementId == 0) {
            return;
        }

        if (UIElement* focused = FindById(mFocusedElementId)) {
            if (auto* inputField = dynamic_cast<UIInputField*>(focused)) {
                if (!inputField->readOnly) {
                    inputField->text += text;
                    if (!inputField->events.onValueChanged.empty()) {
                        TriggerEvent(inputField->events.onValueChanged);
                    }
                }
            }
        }
    }

    void UIManager::HandleBackspace() {
        if (!mFocusedScreen || mFocusedElementId == 0) {
            return;
        }

        if (UIElement* focused = FindById(mFocusedElementId)) {
            if (auto* inputField = dynamic_cast<UIInputField*>(focused)) {
                if (!inputField->readOnly && !inputField->text.empty()) {
                    inputField->text.pop_back();
                    if (!inputField->events.onValueChanged.empty()) {
                        TriggerEvent(inputField->events.onValueChanged);
                    }
                }
            }
        }
    }

    void UIManager::ClearInputState() {
        mHoveredScreen = nullptr;
        mHoveredElementId = 0;
        mPressedScreen = nullptr;
        mPressedElementId = 0;
        mFocusedScreen = nullptr;
        mFocusedElementId = 0;
        mMousePosition = glm::vec2(-10000.0f);
        ClearPressedStates();
    }

    void UIManager::RegisterEventHandler(const std::string& eventName, EventCallback callback) {
        if (eventName.empty() || !callback) {
            return;
        }

        mEventHandlers[eventName] = std::move(callback);
    }

    UIScreen* UIManager::GetPrimaryScreen() {
        return mLoadedScreens.empty() ? nullptr : mLoadedScreens.front().screen.get();
    }

    const UIScreen* UIManager::GetPrimaryScreen() const {
        return mLoadedScreens.empty() ? nullptr : mLoadedScreens.front().screen.get();
    }

    UIAnimator* UIManager::GetAnimator(std::string_view screenName) {
        if (LoadedScreen* loadedScreen = FindLoadedScreen(screenName)) {
            return &loadedScreen->animator;
        }
        return nullptr;
    }

    const UIAnimator* UIManager::GetAnimator(std::string_view screenName) const {
        if (const LoadedScreen* loadedScreen = FindLoadedScreen(screenName)) {
            return &loadedScreen->animator;
        }
        return nullptr;
    }

    UIScreen* UIManager::GetScreen(std::string_view name) {
        if (LoadedScreen* loadedScreen = FindLoadedScreen(name)) {
            return loadedScreen->screen.get();
        }
        return nullptr;
    }

    const UIScreen* UIManager::GetScreen(std::string_view name) const {
        if (const LoadedScreen* loadedScreen = FindLoadedScreen(name)) {
            return loadedScreen->screen.get();
        }
        return nullptr;
    }

    bool UIManager::IsScreenLoaded(std::string_view name) const {
        return FindLoadedScreen(name) != nullptr;
    }

    bool UIManager::HasVisibleScreen() const {
        return std::any_of(mLoadedScreens.begin(), mLoadedScreens.end(), [](const LoadedScreen& loadedScreen) {
            return loadedScreen.screen && loadedScreen.screen->IsVisible();
        });
    }

    std::size_t UIManager::GetVisibleScreenCount() const {
        return static_cast<std::size_t>(std::count_if(mLoadedScreens.begin(), mLoadedScreens.end(), [](const LoadedScreen& loadedScreen) {
            return loadedScreen.screen && loadedScreen.screen->IsVisible();
        }));
    }

    std::vector<std::string> UIManager::GetVisibleScreenNames() const {
        std::vector<std::string> names;
        names.reserve(mLoadedScreens.size());
        for (const auto& loadedScreen : mLoadedScreens) {
            if (!loadedScreen.screen || !loadedScreen.screen->IsVisible()) {
                continue;
            }

            std::string name = loadedScreen.screen->GetName();
            if (loadedScreen.screen->BlocksLowerLayerInput()) {
                name += " [Block]";
            }
            else if (loadedScreen.screen->AllowInputPassthrough()) {
                name += " [Pass]";
            }
            names.push_back(std::move(name));
        }
        return names;
    }

    UIElement* UIManager::FindById(UIElementId id) {
        for (auto it = mLoadedScreens.rbegin(); it != mLoadedScreens.rend(); ++it) {
            if (!it->screen || !it->screen->IsVisible()) {
                continue;
            }
            if (UIElement* found = FindByIdInScreen(*it->screen, id)) {
                return found;
            }
        }
        return nullptr;
    }

    const UIElement* UIManager::FindById(UIElementId id) const {
        for (auto it = mLoadedScreens.rbegin(); it != mLoadedScreens.rend(); ++it) {
            if (!it->screen || !it->screen->IsVisible()) {
                continue;
            }
            if (const UIElement* found = FindByIdInScreen(*it->screen, id)) {
                return found;
            }
        }
        return nullptr;
    }

    const UIElement* UIManager::DebugHitTestElement(const glm::vec2& point, std::string* outScreenName, UIElementId* outElementId) const {
        const HitResult hitResult = HitTest(point);
        if (outScreenName) {
            outScreenName->clear();
        }
        if (outElementId) {
            *outElementId = 0;
        }
        if (!hitResult.element || !hitResult.screen) {
            return nullptr;
        }

        if (outScreenName) {
            *outScreenName = hitResult.screen->GetName();
        }
        if (outElementId) {
            *outElementId = hitResult.elementId;
        }
        return hitResult.element;
    }

    UIManager::HitResult UIManager::HitTest(const glm::vec2& point) const {
        for (auto it = mLoadedScreens.rbegin(); it != mLoadedScreens.rend(); ++it) {
            if (!it->screen || !it->screen->IsVisible() || !it->screen->GetRootCanvas()) {
                continue;
            }

            const UIRect rootRect{
                glm::vec2(0.0f),
                it->screen->GetReferenceResolution(),
                0.0f
            };

            int drawOrder = 0;
            std::vector<UiHitCandidate> candidates;
            candidates.reserve(32);
            CollectHitCandidates(*it->screen->GetRootCanvas(), rootRect, point, drawOrder, candidates);
            if (!candidates.empty()) {
                const auto topmost = std::max_element(candidates.begin(), candidates.end(), [](const UiHitCandidate& lhs, const UiHitCandidate& rhs) {
                    if (lhs.zOrder != rhs.zOrder) {
                        return lhs.zOrder < rhs.zOrder;
                    }
                    return lhs.drawOrder < rhs.drawOrder;
                });

                if (topmost != candidates.end()) {
                    return HitResult{
                        it->screen.get(),
                        topmost->element,
                        topmost->elementId
                    };
                }
            }

            if (it->screen->BlocksLowerLayerInput() || IsScreenTransitionBlockingInput(*it)) {
                return {};
            }
        }

        return {};
    }

    bool UIManager::IsInteractive(const UIElement& element) const {
        return element.visible && element.enabled && element.interactable &&
            (element.GetType() == UIElementType::Button ||
             element.GetType() == UIElementType::Toggle ||
             element.GetType() == UIElementType::Slider ||
             element.GetType() == UIElementType::InputField);
    }

    std::vector<std::string> UIManager::GetRegisteredEventNames() const {
        std::vector<std::string> names;
        names.reserve(mEventHandlers.size());
        for (const auto& [eventName, _] : mEventHandlers) {
            names.push_back(eventName);
        }
        std::sort(names.begin(), names.end());
        return names;
    }

    void UIManager::TriggerEvent(const std::string& eventName) {
        if (eventName.empty()) {
            return;
        }

        const auto it = mEventHandlers.find(eventName);
        const bool hadHandler = it != mEventHandlers.end();

        // Always record the event so the runtime debug panel can show recent activity,
        // even when the event was unhandled.
        EventLogEntry entry{ eventName, hadHandler, ++mEventSequence };
        mRecentEvents.push_back(std::move(entry));
        while (mRecentEvents.size() > kMaxRecentEvents) {
            mRecentEvents.pop_front();
        }

        if (!hadHandler) {
            EngineUi::LogPrint("[UIManager] Event '{}' fired with no registered handler\n", eventName);
            return;
        }

        it->second(eventName);
    }

} // namespace engine
