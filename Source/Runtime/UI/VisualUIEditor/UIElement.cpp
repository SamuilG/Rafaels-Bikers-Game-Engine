#include "UIElement.hpp"

#include <algorithm>
#include <atomic>
#include <stdexcept>

namespace engine {

    namespace {

        // 全局递增 UI 元素 ID 生成器。
        // 运行时和编辑器共用同一条 ID 分配链，方便序列化后继续追加元素。
        std::atomic<UIElementId> gNextUiElementId = 1;

        // 原子递增，获取下一个可用 ID。
        UIElementId GenerateUiElementId() {
            return gNextUiElementId.fetch_add(1, std::memory_order_relaxed);
        }

    } // namespace

    void ReserveUIElementId(UIElementId usedId) {
        // 加载旧文件时，把 ID 分配器推进到已使用 ID 之后，避免后续新建元素撞号。
        UIElementId desired = usedId + 1;
        UIElementId current = gNextUiElementId.load(std::memory_order_relaxed);

        while (current < desired &&
            !gNextUiElementId.compare_exchange_weak(current, desired, std::memory_order_relaxed)) {
        }
    }

    // 元素类型枚举 -> 字符串（序列化 / 调试输出用）。
    std::string_view ToString(UIElementType type) {
        switch (type) {
        case UIElementType::Canvas: return "Canvas";
        case UIElementType::Panel: return "Panel";
        case UIElementType::Image: return "Image";
        case UIElementType::Text: return "Text";
        case UIElementType::Button: return "Button";
        case UIElementType::Slider: return "Slider";
        case UIElementType::Toggle: return "Toggle";
        case UIElementType::ProgressBar: return "ProgressBar";
        case UIElementType::ScrollView: return "ScrollView";
        case UIElementType::InputField: return "InputField";
        case UIElementType::HorizontalLayout: return "HorizontalLayout";
        case UIElementType::VerticalLayout: return "VerticalLayout";
        case UIElementType::GridLayout: return "GridLayout";
        }

        return "Unknown";
    }

    // 按钮过渡模式枚举 -> 字符串。
    std::string_view ToString(UIButtonTransitionMode mode) {
        switch (mode) {
        case UIButtonTransitionMode::None: return "None";
        case UIButtonTransitionMode::ColorTint: return "ColorTint";
        case UIButtonTransitionMode::Scale: return "Scale";
        case UIButtonTransitionMode::Animation: return "Animation";
        }

        return "Animation";
    }

    // 字符串 -> 按钮过渡模式枚举。匹配失败返回 false。
    bool TryParseUIButtonTransitionMode(std::string_view name, UIButtonTransitionMode& outMode) {
        if (name == "None") {
            outMode = UIButtonTransitionMode::None;
            return true;
        }
        if (name == "ColorTint") {
            outMode = UIButtonTransitionMode::ColorTint;
            return true;
        }
        if (name == "Scale") {
            outMode = UIButtonTransitionMode::Scale;
            return true;
        }
        if (name == "Animation") {
            outMode = UIButtonTransitionMode::Animation;
            return true;
        }

        return false;
    }

    UIElement::UIElement(UIElementType elementType, std::string elementName)
        : id(GenerateUiElementId())
        , name(std::move(elementName))
        , type(elementType) {
    }

    UIElement& UIElement::AddChild(std::unique_ptr<UIElement> child) {
        if (!child) {
            throw std::invalid_argument("UIElement::AddChild requires a valid child");
        }

        child->SetParent(this);
        // UI 树的所有权始终由父节点独占持有。
        children.emplace_back(std::move(child));
        return *children.back();
    }

    bool UIElement::RemoveChild(UIElementId childId) {
        // 先尝试删除直接子节点，再递归向下查找。
        for (auto it = children.begin(); it != children.end(); ++it) {
            if ((*it)->GetId() == childId) {
                (*it)->SetParent(nullptr);
                children.erase(it);
                return true;
            }
        }

        for (auto& child : children) {
            if (child->RemoveChild(childId)) {
                return true;
            }
        }

        return false;
    }

    // 深度优先按 ID 查找，先检查自身再递归子节点。
    UIElement* UIElement::FindById(UIElementId searchId) {
        if (id == searchId) {
            return this;
        }

        for (auto& child : children) {
            if (UIElement* found = child->FindById(searchId)) {
                return found;
            }
        }

        return nullptr;
    }

    const UIElement* UIElement::FindById(UIElementId searchId) const {
        if (id == searchId) {
            return this;
        }

        for (const auto& child : children) {
            if (const UIElement* found = child->FindById(searchId)) {
                return found;
            }
        }

        return nullptr;
    }

    UIElement* UIElement::FindByName(std::string_view searchName) {
        if (name == searchName) {
            return this;
        }

        for (auto& child : children) {
            if (UIElement* found = child->FindByName(searchName)) {
                return found;
            }
        }

        return nullptr;
    }

    const UIElement* UIElement::FindByName(std::string_view searchName) const {
        if (name == searchName) {
            return this;
        }

        for (const auto& child : children) {
            if (const UIElement* found = child->FindByName(searchName)) {
                return found;
            }
        }

        return nullptr;
    }

    UIPanel::UIPanel(std::string elementName)
        : UIElement(UIElementType::Panel, std::move(elementName)) {
    }

    UIImage::UIImage(std::string elementName, std::string imageTexturePath)
        : UIElement(UIElementType::Image, std::move(elementName))
        , imagePath(std::move(imageTexturePath)) {
        style.texturePath = imagePath;
    }

    UIText::UIText(std::string elementName, std::string textValue)
        : UIElement(UIElementType::Text, std::move(elementName))
        , text(std::move(textValue)) {
        interactable = false;
    }

    UIButton::UIButton(std::string elementName, std::string buttonLabel)
        : UIElement(UIElementType::Button, std::move(elementName))
        , label(std::move(buttonLabel)) {
        runtimeVisualColor = normalColor;
        runtimeVisualScale = normalScale;
    }

    UISlider::UISlider(std::string elementName)
        : UIElement(UIElementType::Slider, std::move(elementName)) {
    }

    UIToggle::UIToggle(std::string elementName, std::string toggleLabel)
        : UIElement(UIElementType::Toggle, std::move(elementName))
        , label(std::move(toggleLabel)) {
    }

    UIProgressBar::UIProgressBar(std::string elementName)
        : UIElement(UIElementType::ProgressBar, std::move(elementName)) {
        interactable = false;
    }

    UIInputField::UIInputField(std::string elementName, std::string fieldText)
        : UIElement(UIElementType::InputField, std::move(elementName))
        , text(std::move(fieldText)) {
    }

} // namespace engine
