#pragma once

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include <glm/glm.hpp>

#include "../EngineUi.hpp"
#include "GameUIEventRouter.hpp"
#include "ImGuiPreviewRenderer.hpp"
#include "UIManager.hpp"

namespace engine {

    struct UserState;
    class RuntimeUiController;

    struct RuntimeUiElementOptions {
        std::optional<glm::vec2> position;
        std::optional<glm::vec2> size;
        std::optional<glm::vec2> scale;
        std::optional<float> rotation;
        std::optional<float> opacity;
        std::optional<float> fontSize;
        std::optional<float> borderWidth;
        std::optional<float> borderRadius;
        std::optional<glm::vec4> backgroundColor;
        std::optional<glm::vec4> tintColor;
        std::optional<glm::vec4> textColor;
        std::optional<glm::vec4> borderColor;
        std::optional<bool> visible;
        std::optional<bool> enabled;
        std::optional<bool> interactable;
        std::optional<bool> runtimeMutable;
    };

    struct RuntimeUiTextOptions : RuntimeUiElementOptions {
        std::optional<std::string> text;
        std::optional<std::string> alignment;
        std::optional<bool> wrapText;
    };

    struct RuntimeUiImageOptions : RuntimeUiElementOptions {
        std::optional<std::string> imagePath;
        std::optional<bool> preserveAspectRatio;
    };

    struct RuntimeUiButtonOptions : RuntimeUiElementOptions {
        std::optional<std::string> label;
        std::optional<glm::vec4> normalColor;
        std::optional<glm::vec4> hoverColor;
        std::optional<glm::vec4> pressedColor;
        std::optional<glm::vec4> disabledColor;
        std::optional<float> normalScale;
        std::optional<float> hoverScale;
        std::optional<float> pressedScale;
        std::optional<float> transitionDuration;
        std::optional<bool> usePresetTransitionStyle;
    };

    class RuntimeUiWidget final {
    public:
        RuntimeUiWidget() = default;
        RuntimeUiWidget(RuntimeUiController* controller, std::filesystem::path widgetPath)
            : mController(controller)
            , mPath(std::move(widgetPath)) {
        }

        bool IsValid() const { return mController != nullptr; }
        const std::filesystem::path& GetPath() const { return mPath; }

        bool Preload() const;
        bool AddToViewPort() const;
        bool RemoveFromViewPort() const;
        bool Unload() const;
        bool Reload() const;
        bool IsLoaded() const;
        bool IsVisible() const;

        bool SetText(std::string_view elementName, const RuntimeUiTextOptions& options) const;
        bool SetImage(std::string_view elementName, const RuntimeUiImageOptions& options) const;
        bool SetButton(std::string_view elementName, const RuntimeUiButtonOptions& options) const;
        bool SetElementVisible(std::string_view elementName, bool visible) const;

    private:
        RuntimeUiController* mController = nullptr;
        std::filesystem::path mPath;
    };

    class RuntimeUiController final {
    public:
        using TextureResolver = std::function<void*(const std::string&)>;

        RuntimeUiController(bool& appRunning, UserState& state)
            : mAppRunning(appRunning)
            , mState(state) {
        }

        void Initialize(TextureResolver textureResolver) {
            if (mUiManager) {
                return;
            }

            mUiManager = std::make_unique<UIManager>();
            mRenderer = std::make_shared<ImGuiPreviewRenderer>();
            mRenderer->SetTextureResolver(std::move(textureResolver));
            mUiManager->SetRenderer(mRenderer);

            mEventRouter = std::make_unique<GameUIEventRouter>(*this, mState, mAppRunning);
            mEventRouter->Bind(*mUiManager);
        }

        bool IsInitialized() const {
            return static_cast<bool>(mUiManager);
        }

        UIManager* GetManager() {
            return mUiManager.get();
        }

        const UIManager* GetManager() const {
            return mUiManager.get();
        }

        const std::shared_ptr<ImGuiPreviewRenderer>& GetRendererShared() const {
            return mRenderer;
        }

        RuntimeUiWidget GetWidget(const std::filesystem::path& path) {
            return RuntimeUiWidget(this, NormalizeWidgetPath(path));
        }

        bool PreloadWidget(const std::filesystem::path& path) {
            UIManager* manager = RequireManager("PreloadWidget");
            if (!manager) {
                return false;
            }

            const std::filesystem::path normalizedPath = NormalizeWidgetPath(path);
            const std::string screenName = BuildScreenNameFromPath(normalizedPath);
            if (screenName.empty()) {
                return false;
            }

            if (manager->IsScreenLoaded(screenName)) {
                return true;
            }

            if (!manager->LoadScreen(screenName, normalizedPath)) {
                return false;
            }

            if (UIScreen* screen = manager->GetScreen(screenName)) {
                screen->SetVisible(false);
            }
            return true;
        }

        bool AddWidgetToViewPort(const std::filesystem::path& path) {
            UIManager* manager = RequireManager("AddWidgetToViewPort");
            if (!manager) {
                return false;
            }

            const std::filesystem::path normalizedPath = NormalizeWidgetPath(path);
            const std::string screenName = BuildScreenNameFromPath(normalizedPath);
            if (screenName.empty()) {
                EngineUi::LogPrint("[RuntimeUI] AddWidgetToViewPort failed: empty screen name for {}\n", normalizedPath.generic_string());
                return false;
            }

            if (!manager->IsScreenLoaded(screenName) && !manager->LoadScreen(screenName, normalizedPath)) {
                return false;
            }

            manager->PushScreen(screenName);
            return true;
        }

        bool RemoveWidgetFromViewPort(const std::filesystem::path& path) {
            UIManager* manager = RequireManager("RemoveWidgetFromViewPort");
            if (!manager) {
                return false;
            }

            const std::string screenName = BuildScreenNameFromPath(NormalizeWidgetPath(path));
            if (!manager->IsScreenLoaded(screenName)) {
                return false;
            }

            manager->HideScreen(screenName);
            return true;
        }

        bool UnloadWidget(const std::filesystem::path& path) {
            UIManager* manager = RequireManager("UnloadWidget");
            if (!manager) {
                return false;
            }

            return manager->UnloadScreen(BuildScreenNameFromPath(NormalizeWidgetPath(path)));
        }

        bool ReloadWidget(const std::filesystem::path& path) {
            UIManager* manager = RequireManager("ReloadWidget");
            if (!manager) {
                return false;
            }

            const std::filesystem::path normalizedPath = NormalizeWidgetPath(path);
            const std::string screenName = BuildScreenNameFromPath(normalizedPath);
            if (!manager->IsScreenLoaded(screenName)) {
                return false;
            }

            return manager->LoadScreen(screenName, normalizedPath);
        }

        bool IsWidgetLoaded(const std::filesystem::path& path) const {
            const UIManager* manager = RequireManager("IsWidgetLoaded");
            return manager ? manager->IsScreenLoaded(BuildScreenNameFromPath(NormalizeWidgetPath(path))) : false;
        }

        bool IsWidgetVisible(const std::filesystem::path& path) const {
            const UIManager* manager = RequireManager("IsWidgetVisible");
            if (!manager) {
                return false;
            }

            if (const UIScreen* screen = manager->GetScreen(BuildScreenNameFromPath(NormalizeWidgetPath(path)))) {
                return screen->IsVisible();
            }
            return false;
        }

        bool SetText(const std::filesystem::path& path, std::string_view elementName, const RuntimeUiTextOptions& options) {
            if (UIText* text = FindWidgetElementAs<UIText>(path, elementName, "SetText")) {
                ApplyElementOptions(*text, options);
                if (options.text.has_value()) {
                    text->text = *options.text;
                }
                if (options.alignment.has_value()) {
                    text->alignment = *options.alignment;
                }
                if (options.wrapText.has_value()) {
                    text->wrapText = *options.wrapText;
                }
                return true;
            }
            return false;
        }

        bool SetImage(const std::filesystem::path& path, std::string_view elementName, const RuntimeUiImageOptions& options) {
            if (UIImage* image = FindWidgetElementAs<UIImage>(path, elementName, "SetImage")) {
                ApplyElementOptions(*image, options);
                if (options.imagePath.has_value()) {
                    image->imagePath = *options.imagePath;
                    image->style.texturePath = *options.imagePath;
                }
                if (options.preserveAspectRatio.has_value()) {
                    image->preserveAspectRatio = *options.preserveAspectRatio;
                }
                return true;
            }
            return false;
        }

        bool SetButton(const std::filesystem::path& path, std::string_view elementName, const RuntimeUiButtonOptions& options) {
            if (UIButton* button = FindWidgetElementAs<UIButton>(path, elementName, "SetButton")) {
                ApplyElementOptions(*button, options);
                if (options.label.has_value()) {
                    button->label = *options.label;
                }
                if (options.usePresetTransitionStyle.has_value()) {
                    button->usePresetTransitionStyle = *options.usePresetTransitionStyle;
                }
                if (options.normalColor.has_value()) {
                    button->usePresetTransitionStyle = false;
                    button->normalColor = *options.normalColor;
                    button->style.backgroundColor = *options.normalColor;
                }
                if (options.hoverColor.has_value()) {
                    button->usePresetTransitionStyle = false;
                    button->hoverColor = *options.hoverColor;
                }
                if (options.pressedColor.has_value()) {
                    button->usePresetTransitionStyle = false;
                    button->pressedColor = *options.pressedColor;
                }
                if (options.disabledColor.has_value()) {
                    button->usePresetTransitionStyle = false;
                    button->disabledColor = *options.disabledColor;
                }
                if (options.normalScale.has_value()) {
                    button->usePresetTransitionStyle = false;
                    button->normalScale = *options.normalScale;
                }
                if (options.hoverScale.has_value()) {
                    button->usePresetTransitionStyle = false;
                    button->hoverScale = *options.hoverScale;
                }
                if (options.pressedScale.has_value()) {
                    button->usePresetTransitionStyle = false;
                    button->pressedScale = *options.pressedScale;
                }
                if (options.transitionDuration.has_value()) {
                    button->usePresetTransitionStyle = false;
                    button->transitionDuration = *options.transitionDuration;
                }

                button->runtimeVisualColor = button->normalColor;
                button->runtimeVisualScale = button->normalScale;
                button->runtimeVisualInitialized = false;
                return true;
            }
            return false;
        }

        bool SetElementVisible(const std::filesystem::path& path, std::string_view elementName, bool visible) {
            if (UIElement* element = FindWidgetElement(path, elementName, "SetElementVisible")) {
                if (!element->runtimeMutable) {
                    EngineUi::LogPrint("[RuntimeUI] SetElementVisible skipped: '{}' in '{}' is not runtime mutable\n", elementName, path.generic_string());
                    return false;
                }
                element->visible = visible;
                return true;
            }
            return false;
        }

        static std::filesystem::path NormalizeWidgetPath(const std::filesystem::path& path) {
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

        static std::string BuildScreenNameFromPath(const std::filesystem::path& path) {
            std::string screenName = path.filename().string();
            if (screenName.empty()) {
                return {};
            }

            constexpr std::string_view kUiJsonSuffix = ".ui.json";
            constexpr std::string_view kJsonSuffix = ".json";

            if (screenName.ends_with(kUiJsonSuffix)) {
                screenName.erase(screenName.size() - kUiJsonSuffix.size());
            }
            else if (screenName.ends_with(kJsonSuffix)) {
                screenName.erase(screenName.size() - kJsonSuffix.size());
            }
            else if (path.has_extension()) {
                screenName = path.stem().string();
            }

            return screenName;
        }

    private:
        UIManager* RequireManager(std::string_view operation) {
            if (!mUiManager) {
                EngineUi::LogPrint("[RuntimeUI] {} requested before RuntimeUiController init\n", operation);
            }
            return mUiManager.get();
        }

        const UIManager* RequireManager(std::string_view operation) const {
            if (!mUiManager) {
                EngineUi::LogPrint("[RuntimeUI] {} requested before RuntimeUiController init\n", operation);
            }
            return mUiManager.get();
        }

        UIScreen* GetWidgetScreen(const std::filesystem::path& path) {
            UIManager* manager = RequireManager("GetWidgetScreen");
            if (!manager) {
                return nullptr;
            }

            return manager->GetScreen(BuildScreenNameFromPath(NormalizeWidgetPath(path)));
        }

        UIElement* FindWidgetElement(const std::filesystem::path& path, std::string_view elementName, std::string_view operation) {
            if (elementName.empty()) {
                EngineUi::LogPrint("[RuntimeUI] {} failed: empty element name for '{}'\n", operation, path.generic_string());
                return nullptr;
            }

            UIScreen* screen = GetWidgetScreen(path);
            if (!screen) {
                EngineUi::LogPrint("[RuntimeUI] {} failed: widget '{}' is not loaded\n", operation, path.generic_string());
                return nullptr;
            }

            UIElement* element = screen->FindByName(elementName);
            if (!element) {
                EngineUi::LogPrint("[RuntimeUI] {} failed: element '{}' not found in '{}'\n", operation, elementName, path.generic_string());
            }
            return element;
        }

        template <typename ElementT>
        ElementT* FindWidgetElementAs(const std::filesystem::path& path, std::string_view elementName, std::string_view operation) {
            UIElement* element = FindWidgetElement(path, elementName, operation);
            if (!element) {
                return nullptr;
            }
            if (!element->runtimeMutable) {
                EngineUi::LogPrint("[RuntimeUI] {} skipped: '{}' in '{}' is not runtime mutable\n", operation, elementName, path.generic_string());
                return nullptr;
            }

            ElementT* typedElement = dynamic_cast<ElementT*>(element);
            if (!typedElement) {
                EngineUi::LogPrint("[RuntimeUI] {} failed: element '{}' in '{}' has the wrong type\n", operation, elementName, path.generic_string());
            }
            return typedElement;
        }

        static void ApplyElementOptions(UIElement& element, const RuntimeUiElementOptions& options) {
            if (options.position.has_value()) {
                element.transform.position = *options.position;
            }
            if (options.size.has_value()) {
                element.transform.size = *options.size;
            }
            if (options.scale.has_value()) {
                element.transform.scale = *options.scale;
            }
            if (options.rotation.has_value()) {
                element.transform.rotation = *options.rotation;
            }
            if (options.opacity.has_value()) {
                element.style.opacity = std::clamp(*options.opacity, 0.0f, 1.0f);
            }
            if (options.fontSize.has_value()) {
                element.style.fontSize = *options.fontSize;
            }
            if (options.borderWidth.has_value()) {
                element.style.borderWidth = *options.borderWidth;
            }
            if (options.borderRadius.has_value()) {
                element.style.borderRadius = *options.borderRadius;
            }
            if (options.backgroundColor.has_value()) {
                element.style.backgroundColor = *options.backgroundColor;
            }
            if (options.tintColor.has_value()) {
                element.style.tintColor = *options.tintColor;
            }
            if (options.textColor.has_value()) {
                element.style.textColor = *options.textColor;
            }
            if (options.borderColor.has_value()) {
                element.style.borderColor = *options.borderColor;
            }
            if (options.visible.has_value()) {
                element.visible = *options.visible;
            }
            if (options.enabled.has_value()) {
                element.enabled = *options.enabled;
            }
            if (options.interactable.has_value()) {
                element.interactable = *options.interactable;
            }
            if (options.runtimeMutable.has_value()) {
                element.runtimeMutable = *options.runtimeMutable;
            }
        }

    private:
        bool& mAppRunning;
        UserState& mState;
        std::unique_ptr<UIManager> mUiManager;
        std::shared_ptr<ImGuiPreviewRenderer> mRenderer;
        std::unique_ptr<GameUIEventRouter> mEventRouter;
    };

    inline bool RuntimeUiWidget::Preload() const {
        return mController && mController->PreloadWidget(mPath);
    }

    inline bool RuntimeUiWidget::AddToViewPort() const {
        return mController && mController->AddWidgetToViewPort(mPath);
    }

    inline bool RuntimeUiWidget::RemoveFromViewPort() const {
        return mController && mController->RemoveWidgetFromViewPort(mPath);
    }

    inline bool RuntimeUiWidget::Unload() const {
        return mController && mController->UnloadWidget(mPath);
    }

    inline bool RuntimeUiWidget::Reload() const {
        return mController && mController->ReloadWidget(mPath);
    }

    inline bool RuntimeUiWidget::IsLoaded() const {
        return mController && mController->IsWidgetLoaded(mPath);
    }

    inline bool RuntimeUiWidget::IsVisible() const {
        return mController && mController->IsWidgetVisible(mPath);
    }

    inline bool RuntimeUiWidget::SetText(std::string_view elementName, const RuntimeUiTextOptions& options) const {
        return mController && mController->SetText(mPath, elementName, options);
    }

    inline bool RuntimeUiWidget::SetImage(std::string_view elementName, const RuntimeUiImageOptions& options) const {
        return mController && mController->SetImage(mPath, elementName, options);
    }

    inline bool RuntimeUiWidget::SetButton(std::string_view elementName, const RuntimeUiButtonOptions& options) const {
        return mController && mController->SetButton(mPath, elementName, options);
    }

    inline bool RuntimeUiWidget::SetElementVisible(std::string_view elementName, bool visible) const {
        return mController && mController->SetElementVisible(mPath, elementName, visible);
    }

} // namespace engine
