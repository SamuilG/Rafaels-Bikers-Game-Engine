#include "UICompiler.hpp"

#include <cmath>
#include <format>
#include <unordered_set>
#include <vector>

#include "../EngineUi.hpp"
#include "UIAnimation.hpp"
#include "UIBinding.hpp"
#include "UIElement.hpp"
#include "UIManager.hpp"
#include "UIScreen.hpp"
#include "UISerializer.hpp"
#include "UIStyle.hpp"
#include "UITheme.hpp"

namespace engine {

    namespace {

        bool IsFiniteVec2(const glm::vec2& value) {
            return std::isfinite(value.x) && std::isfinite(value.y);
        }

        bool IsFiniteVec4(const glm::vec4& value) {
            return std::isfinite(value.x) && std::isfinite(value.y) &&
                std::isfinite(value.z) && std::isfinite(value.w);
        }

        bool IsFiniteFloat(float value) {
            return std::isfinite(value) != 0;
        }

        // 把 ui.json 里写的相对路径展开为可在文件系统里检查的路径。
        // 既支持小写 "assets/" 前缀也兼容首字母大写的 "Assets/"。
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

        // 拼出 "Canvas/Panel/Button" 这样的可读路径，让校验信息能定位到具体节点。
        std::string BuildElementPath(const UIElement& element) {
            std::vector<std::string> parts;
            const UIElement* current = &element;
            while (current) {
                parts.push_back(current->GetName().empty() ? std::string("<unnamed>") : current->GetName());
                current = current->GetParent();
            }
            std::string joined;
            for (auto it = parts.rbegin(); it != parts.rend(); ++it) {
                if (!joined.empty()) {
                    joined += "/";
                }
                joined += *it;
            }
            return joined;
        }

        // 编译输出文件名：把任何非字母数字字符替换成下划线，避免奇怪的路径。
        std::string MakeCompiledBaseName(const UIScreen& screen, const std::filesystem::path& sourcePath) {
            std::string baseName;
            if (!sourcePath.empty()) {
                baseName = sourcePath.filename().string();
                if (baseName.ends_with(".ui.json")) {
                    baseName.erase(baseName.size() - std::string(".ui.json").size());
                }
            }
            if (baseName.empty()) {
                baseName = screen.GetName().empty() ? std::string("UIScreen") : screen.GetName();
            }
            for (char& ch : baseName) {
                const bool valid =
                    (ch >= 'a' && ch <= 'z') ||
                    (ch >= 'A' && ch <= 'Z') ||
                    (ch >= '0' && ch <= '9') ||
                    ch == '_' || ch == '-';
                if (!valid) {
                    ch = '_';
                }
            }
            return baseName;
        }

        std::filesystem::path BuildCompiledOutputPath(const UIScreen& screen, const std::filesystem::path& sourcePath) {
            const std::string baseName = MakeCompiledBaseName(screen, sourcePath);
            return std::filesystem::path("Assets") / "ui" / "compiled" / (baseName + ".compiled.ui.json");
        }

        // 收集所有元素 ID 与类型，便于绑定/动画引用校验。
        struct ElementIndex {
            std::unordered_map<UIElementId, UIElementType> typeById;
            std::unordered_map<UIElementId, std::string> pathById;
        };

        void IndexElements(const UIElement& element, ElementIndex& outIndex) {
            outIndex.typeById[element.GetId()] = element.GetType();
            outIndex.pathById[element.GetId()] = BuildElementPath(element);
            for (const auto& child : element.GetChildren()) {
                IndexElements(*child, outIndex);
            }
        }

        // 收集元素树上出现的所有事件名，用于和 UIManager 已注册事件做对账。
        void CollectEventNames(const UIElement& element, std::vector<std::pair<std::string, std::string>>& outEvents) {
            const std::string elementPath = BuildElementPath(element);
            const auto record = [&](const std::string& eventName) {
                if (!eventName.empty()) {
                    outEvents.emplace_back(eventName, elementPath);
                }
            };
            record(element.events.onClick);
            record(element.events.onHover);
            record(element.events.onPressed);
            record(element.events.onReleased);
            record(element.events.onValueChanged);
            for (const auto& child : element.GetChildren()) {
                CollectEventNames(*child, outEvents);
            }
        }

        // 检查给定的目标属性是否能合理地映射到某个元素类型。
        // 例如 ProgressBar.value 必须挂在 ProgressBar 上，否则在运行时静默失败。
        bool IsBindingTargetCompatible(UIBindingTargetProperty property, UIElementType type) {
            switch (property) {
            case UIBindingTargetProperty::TextText:
                return type == UIElementType::Text || type == UIElementType::Button;
            case UIBindingTargetProperty::ProgressBarValue:
                return type == UIElementType::ProgressBar;
            case UIBindingTargetProperty::SliderValue:
                return type == UIElementType::Slider;
            case UIBindingTargetProperty::ImageTintColor:
                return type == UIElementType::Image || type == UIElementType::Panel;
            case UIBindingTargetProperty::ElementVisible:
            case UIBindingTargetProperty::ElementOpacity:
            case UIBindingTargetProperty::ElementPosition:
            case UIBindingTargetProperty::ElementRotation:
                return true;
            }
            return true;
        }

        void ValidateElement(
            const UIElement& element,
            std::unordered_set<UIElementId>& seenIds,
            UICompiler::Result& result)
        {
            const std::string elementPath = BuildElementPath(element);

            if (!seenIds.insert(element.GetId()).second) {
                result.errors.push_back(std::format("Duplicate element id {} at {}", element.GetId(), elementPath));
            }
            if (element.GetName().empty()) {
                result.errors.push_back(std::format("Element id {} has an empty name", element.GetId()));
            }

            const UITransform& transform = element.transform;
            if (!IsFiniteVec2(transform.position) ||
                !IsFiniteVec2(transform.size) ||
                !IsFiniteVec2(transform.anchorMin) ||
                !IsFiniteVec2(transform.anchorMax) ||
                !IsFiniteVec2(transform.pivot) ||
                !IsFiniteFloat(transform.rotation) ||
                !IsFiniteVec2(transform.scale)) {
                result.errors.push_back(std::format("Invalid transform values at {}", elementPath));
            }

            if (element.GetType() != UIElementType::Canvas &&
                (transform.size.x <= 0.0f || transform.size.y <= 0.0f)) {
                result.errors.push_back(std::format("Element size must be positive at {}", elementPath));
            }

            if (!IsFiniteVec4(element.style.backgroundColor) ||
                !IsFiniteVec4(element.style.tintColor) ||
                !IsFiniteVec4(element.style.textColor) ||
                !IsFiniteVec4(element.style.borderColor) ||
                !IsFiniteFloat(element.style.opacity) ||
                !IsFiniteFloat(element.style.fontSize) ||
                !IsFiniteFloat(element.style.borderWidth) ||
                !IsFiniteFloat(element.style.borderRadius)) {
                result.errors.push_back(std::format("Invalid style values at {}", elementPath));
            }

            if (!element.style.texturePath.empty()) {
                const std::filesystem::path texturePath = ResolveAssetPath(element.style.texturePath);
                if (!std::filesystem::exists(texturePath)) {
                    result.errors.push_back(std::format("Texture path does not exist at {}: {}", elementPath, texturePath.generic_string()));
                }
            }

            if (!element.style.fontPath.empty()) {
                const std::filesystem::path fontPath = ResolveAssetPath(element.style.fontPath);
                if (!std::filesystem::exists(fontPath)) {
                    result.errors.push_back(std::format("Font path does not exist at {}: {}", elementPath, fontPath.generic_string()));
                }
            }

            if (const auto* image = dynamic_cast<const UIImage*>(&element)) {
                if (!image->imagePath.empty()) {
                    const std::filesystem::path imagePath = ResolveAssetPath(image->imagePath);
                    if (!std::filesystem::exists(imagePath)) {
                        result.errors.push_back(std::format("UIImage path does not exist at {}: {}", elementPath, imagePath.generic_string()));
                    }
                }
            }

            if (const auto* button = dynamic_cast<const UIButton*>(&element)) {
                if (button->label.empty()) {
                    result.errors.push_back(std::format("UIButton label is empty at {}", elementPath));
                }
                if (element.events.onClick.empty()) {
                    result.warnings.push_back(std::format("UIButton onClick is empty at {}", elementPath));
                }
            }

            for (const auto& child : element.GetChildren()) {
                ValidateElement(*child, seenIds, result);
            }
        }

        // 校验数据绑定：目标元素必须存在，目标属性必须与元素类型相容，源键必须非空。
        void ValidateBindings(const UIScreen& screen, const ElementIndex& index, UICompiler::Result& result) {
            for (std::size_t bindingIndex = 0; bindingIndex < screen.GetBindings().size(); ++bindingIndex) {
                const UIPropertyBinding& binding = screen.GetBindings()[bindingIndex];
                if (binding.sourceKey.empty()) {
                    result.warnings.push_back(std::format(
                        "Binding [{}] has empty sourceKey (target id={}, property={})",
                        bindingIndex,
                        binding.targetElementId,
                        ToString(binding.targetProperty)));
                }

                if (binding.targetElementId == 0) {
                    result.errors.push_back(std::format(
                        "Binding [{}] has no target element id (sourceKey='{}')",
                        bindingIndex,
                        binding.sourceKey));
                    continue;
                }

                const auto typeIt = index.typeById.find(binding.targetElementId);
                if (typeIt == index.typeById.end()) {
                    result.errors.push_back(std::format(
                        "Binding [{}] target element id {} not found (sourceKey='{}')",
                        bindingIndex,
                        binding.targetElementId,
                        binding.sourceKey));
                    continue;
                }

                if (!IsBindingTargetCompatible(binding.targetProperty, typeIt->second)) {
                    const std::string& targetPath = index.pathById.at(binding.targetElementId);
                    result.warnings.push_back(std::format(
                        "Binding [{}] target property {} is not compatible with element type {} at {}",
                        bindingIndex,
                        ToString(binding.targetProperty),
                        ToString(typeIt->second),
                        targetPath));
                }
            }
        }

        // 校验动画 clip：每条 track 的目标元素必须存在；clip 名称不为空；时间轴正常。
        void ValidateAnimations(const UIScreen& screen, const ElementIndex& index, UICompiler::Result& result) {
            std::unordered_set<std::string> clipNames;
            for (std::size_t clipIndex = 0; clipIndex < screen.GetAnimationClips().size(); ++clipIndex) {
                const UIAnimationClip& clip = screen.GetAnimationClips()[clipIndex];
                if (clip.name.empty()) {
                    result.warnings.push_back(std::format("AnimationClip [{}] has empty name", clipIndex));
                }
                else if (!clipNames.insert(clip.name).second) {
                    result.warnings.push_back(std::format("Duplicate animation clip name '{}'", clip.name));
                }

                if (clip.duration < 0.0f || !std::isfinite(clip.duration)) {
                    result.errors.push_back(std::format("AnimationClip '{}' has invalid duration {}", clip.name, clip.duration));
                }

                for (std::size_t trackIndex = 0; trackIndex < clip.tracks.size(); ++trackIndex) {
                    const UIAnimationTrack& track = clip.tracks[trackIndex];
                    if (track.targetElementId == 0) {
                        result.errors.push_back(std::format(
                            "AnimationClip '{}' track [{}] has no target element id",
                            clip.name,
                            trackIndex));
                        continue;
                    }
                    if (!index.typeById.contains(track.targetElementId)) {
                        result.errors.push_back(std::format(
                            "AnimationClip '{}' track [{}] points at missing element id {}",
                            clip.name,
                            trackIndex,
                            track.targetElementId));
                    }
                    if (track.keyframes.empty()) {
                        result.warnings.push_back(std::format(
                            "AnimationClip '{}' track [{}] has no keyframes",
                            clip.name,
                            trackIndex));
                    }
                }
            }

            // Screen 引用的 enter/exit 动画必须能在 clip 列表里找到。
            const auto requireClip = [&](const std::string& referencedName, const char* fieldName) {
                if (referencedName.empty()) {
                    return;
                }
                if (!clipNames.contains(referencedName)) {
                    result.warnings.push_back(std::format(
                        "Screen references {} animation '{}' that is not defined in clips",
                        fieldName,
                        referencedName));
                }
            };
            requireClip(screen.GetEnterAnimation(), "enter");
            requireClip(screen.GetExitAnimation(), "exit");
        }

        // 校验主题：主题文件存在 + 元素引用的 preset 名能在主题里找到。
        void ValidateTheme(const UIScreen& screen, UICompiler::Result& result) {
            const std::string& themePath = screen.GetThemePath();
            if (themePath.empty()) {
                return;
            }

            const std::filesystem::path resolvedPath = ResolveAssetPath(themePath);
            if (!std::filesystem::exists(resolvedPath)) {
                result.errors.push_back(std::format("Theme path does not exist: {}", resolvedPath.generic_string()));
                return;
            }

            std::shared_ptr<const UITheme> theme = LoadUiTheme(resolvedPath);
            if (!theme) {
                result.warnings.push_back(std::format("Theme could not be parsed: {}", resolvedPath.generic_string()));
                return;
            }

            const UIElement* root = screen.GetRootCanvas();
            if (!root) {
                return;
            }
            const auto inspect = [&](const UIElement& element) {
                if (element.style.presetName.empty()) {
                    return;
                }
                if (!FindStylePreset(*theme, element.style.presetName)) {
                    result.warnings.push_back(std::format(
                        "Style preset '{}' referenced at {} not found in theme",
                        element.style.presetName,
                        BuildElementPath(element)));
                }
            };
            root->Traverse([&](const UIElement& element) { inspect(element); });
        }

        // 校验事件：UIManager 已绑定的事件清单当作权威白名单，
        // 元素里没注册的事件名只发警告，不阻断编译（运行时仍会打印 missing handler）。
        void ValidateEventBindings(const UIScreen& screen, const UIManager* uiManager, UICompiler::Result& result) {
            if (!uiManager) {
                return;
            }
            const UIElement* root = screen.GetRootCanvas();
            if (!root) {
                return;
            }

            std::vector<std::pair<std::string, std::string>> events;
            CollectEventNames(*root, events);
            for (const auto& [eventName, elementPath] : events) {
                if (!uiManager->HasEventHandler(eventName)) {
                    result.warnings.push_back(std::format(
                        "Event '{}' at {} has no registered handler in UIManager",
                        eventName,
                        elementPath));
                }
            }
        }

    } // namespace

    UICompiler::Result UICompiler::Validate(const UIScreen& screen, const UIManager* optionalUiManager) {
        Result result;
        result.outputPath = BuildCompiledOutputPath(screen, {});

        const UIElement* rootCanvas = screen.GetRootCanvas();
        if (!rootCanvas) {
            result.errors.push_back("UIScreen does not have a root Canvas");
        }
        else if (rootCanvas->GetType() != UIElementType::Canvas) {
            result.errors.push_back("UIScreen root element is not a Canvas");
        }

        ElementIndex index;
        if (rootCanvas) {
            std::unordered_set<UIElementId> seenIds;
            ValidateElement(*rootCanvas, seenIds, result);
            IndexElements(*rootCanvas, index);
        }

        ValidateBindings(screen, index, result);
        ValidateAnimations(screen, index, result);
        ValidateTheme(screen, result);
        ValidateEventBindings(screen, optionalUiManager, result);

        result.warningCount = static_cast<int>(result.warnings.size());
        result.errorCount = static_cast<int>(result.errors.size());
        result.success = result.errorCount == 0;
        return result;
    }

    UICompiler::Result UICompiler::Compile(const UIScreen& screen,
                                           const std::filesystem::path& sourcePath,
                                           const UIManager* optionalUiManager) {
        Result result = Validate(screen, optionalUiManager);
        result.outputPath = BuildCompiledOutputPath(screen, sourcePath);

        EngineUi::LogPrint("[UICompiler] Validating screen '{}'\n", screen.GetName());
        for (const std::string& warning : result.warnings) {
            EngineUi::LogPrint("[UICompiler][Warning] {}\n", warning);
        }
        for (const std::string& error : result.errors) {
            EngineUi::LogPrint("[UICompiler][Error] {}\n", error);
        }

        if (!result.success) {
            EngineUi::LogPrint(
                "[UICompiler] Failed with {} error(s) and {} warning(s)\n",
                result.errorCount,
                result.warningCount);
            return result;
        }

        if (!UISerializer::SaveToFile(screen, result.outputPath)) {
            result.success = false;
            result.errors.push_back(std::format(
                "Failed to write compiled file to {}",
                result.outputPath.generic_string()));
            result.errorCount = static_cast<int>(result.errors.size());
            EngineUi::LogPrint("[UICompiler][Error] Failed to write compiled file to {}\n", result.outputPath.generic_string());
            return result;
        }

        EngineUi::LogPrint(
            "[UICompiler] Success: {} warning(s), output {}\n",
            result.warningCount,
            result.outputPath.generic_string());
        return result;
    }

} // namespace engine
