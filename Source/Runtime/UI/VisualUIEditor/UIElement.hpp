#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "UIStyle.hpp"
#include "UITransform.hpp"

namespace engine {

    // UI 元素基类 —— 所有可视控件（Panel、Text、Button 等）的共同父类。
    // 管理树形层级关系、变换、样式和事件占位信息。
    // 不可拷贝 / 移动，所有权由父节点的 unique_ptr 独占持有。
    class UIElement {
    public:
        explicit UIElement(UIElementType elementType, std::string elementName = {});
        virtual ~UIElement() = default;

        // 禁止拷贝和移动，保证树形结构中的指针安全。
        UIElement(const UIElement&) = delete;
        UIElement& operator=(const UIElement&) = delete;
        UIElement(UIElement&&) = delete;
        UIElement& operator=(UIElement&&) = delete;

        UIElementId GetId() const { return id; }
        const std::string& GetName() const { return name; }
        void SetName(std::string elementName) { name = std::move(elementName); }

        UIElementType GetType() const { return type; }
        UIElement* GetParent() { return parent; }
        const UIElement* GetParent() const { return parent; }

        const std::vector<std::unique_ptr<UIElement>>& GetChildren() const { return children; }
        std::vector<std::unique_ptr<UIElement>>& GetChildren() { return children; }

        // 将子元素移入当前节点，设置其父指针并接管所有权。
        UIElement& AddChild(std::unique_ptr<UIElement> child);
        // 按 ID 删除直接子节点或递归子孙节点，成功返回 true。
        bool RemoveChild(UIElementId childId);

        // 原地创建并添加指定类型的子元素，返回新元素引用。
        template <typename T, typename... Args>
        T& CreateChild(Args&&... args) {
            static_assert(std::is_base_of_v<UIElement, T>, "T must derive from UIElement");
            auto child = std::make_unique<T>(std::forward<Args>(args)...);
            T& childRef = *child;
            AddChild(std::move(child));
            return childRef;
        }

        // 在整棵子树中按 ID 查找元素，找不到返回 nullptr。
        UIElement* FindById(UIElementId searchId);
        const UIElement* FindById(UIElementId searchId) const;
        UIElement* FindByName(std::string_view searchName);
        const UIElement* FindByName(std::string_view searchName) const;

        // 深度优先遍历当前节点及其所有子孙节点。
        template <typename Visitor>
        void Traverse(Visitor&& visitor) {
            TraverseImpl(*this, visitor);
        }

        template <typename Visitor>
        void Traverse(Visitor&& visitor) const {
            TraverseImpl(*this, visitor);
        }

    public:
        // 运行时和编辑器共用的通用节点数据。
        UIElementId id = 0;
        std::string name;
        UIElementType type = UIElementType::Panel;
        bool visible = true;
        bool enabled = true;
        bool interactable = true;
        bool runtimeMutable = true;
        int zOrder = 0;
        UITransform transform;
        UIStyle style;
        UIEventPlaceholders events;

    protected:
        void SetParent(UIElement* newParent) { parent = newParent; }

    private:
        // 递归遍历实现，统一处理 const 和非 const 版本。
        template <typename ElementType, typename Visitor>
        static void TraverseImpl(ElementType& element, Visitor& visitor) {
            visitor(element);
            for (auto& child : element.children) {
                TraverseImpl(*child, visitor);
            }
        }

    private:
        UIElement* parent = nullptr;
        std::vector<std::unique_ptr<UIElement>> children;
    };

    // 面板元素 —— 最基本的矩形容器，用于背景、分组和布局。
    class UIPanel final : public UIElement {
    public:
        explicit UIPanel(std::string elementName = "Panel");
    };

    // 图片元素 —— 显示纹理或精灵，可选保持宽高比。
    class UIImage final : public UIElement {
    public:
        explicit UIImage(std::string elementName = "Image", std::string imageTexturePath = {});

    public:
        std::string imagePath;          // 纹理资源路径
        bool preserveAspectRatio = true; // 是否保持原始宽高比
    };

    // 文本元素 —— 显示静态或数据绑定的文字内容。
    class UIText final : public UIElement {
    public:
        explicit UIText(std::string elementName = "Text", std::string textValue = {});

    public:
        std::string text;               // 显示的文本内容
        std::string alignment = "Left"; // 对齐方式: Left / Center / Right
        bool wrapText = true;           // 是否自动换行
    };

    // 按钮元素 —— 支持悬停 / 按下 / 禁用四态过渡的可交互控件。
    class UIButton final : public UIElement {
    public:
        explicit UIButton(std::string elementName = "Button", std::string buttonLabel = {});

    public:
        glm::vec2 backgroundImageScale = glm::vec2(1.0f);//x和y缩放
        std::string label;                          // 按钮上显示的文字
        bool pressed = false;                       // 当前帧是否处于按下状态
        bool usePresetTransitionStyle = true;       // 是否使用主题预设中的过渡样式
        UIButtonTransitionMode transitionMode = UIButtonTransitionMode::Animation; // 过渡模式
        glm::vec4 normalColor = glm::vec4(0.24f, 0.31f, 0.55f, 1.0f);   // 常态背景色
        glm::vec4 hoverColor = glm::vec4(0.32f, 0.40f, 0.66f, 1.0f);    // 悬停背景色
        glm::vec4 pressedColor = glm::vec4(0.18f, 0.24f, 0.42f, 1.0f);  // 按下背景色
        glm::vec4 disabledColor = glm::vec4(0.25f, 0.25f, 0.28f, 0.8f); // 禁用背景色
        float normalScale = 1.0f;           // 常态缩放
        float hoverScale = 1.04f;           // 悬停缩放
        float pressedScale = 0.96f;         // 按下缩放
        float transitionDuration = 0.12f;   // 过渡动画时长（秒）
        glm::vec4 runtimeVisualColor = normalColor; // 运行时插值后的当前颜色
        float runtimeVisualScale = 1.0f;            // 运行时插值后的当前缩放
        bool runtimeVisualInitialized = false;       // 运行时视觉状态是否已初始化
    };

    // 滑动条元素 —— 在 [minValue, maxValue] 范围内拖动选择数值。
    class UISlider final : public UIElement {
    public:
        explicit UISlider(std::string elementName = "Slider");

    public:
        float minValue = 0.0f;      // 最小值
        float maxValue = 1.0f;      // 最大值
        float value = 0.5f;         // 当前值
        bool wholeNumbers = false;  // 是否只取整数
        glm::vec4 fillColor = glm::vec4(0.35f, 0.72f, 0.96f, 1.0f);   // 填充部分颜色
        glm::vec4 handleColor = glm::vec4(0.95f, 0.97f, 1.0f, 1.0f);  // 滑块手柄颜色
    };

    // 开关元素 —— 二态切换控件（开 / 关）。
    class UIToggle final : public UIElement {
    public:
        explicit UIToggle(std::string elementName = "Toggle", std::string toggleLabel = {});

    public:
        std::string label;      // 开关旁边的文字标签
        bool isOn = true;       // 当前是否处于开启状态
        glm::vec4 onColor = glm::vec4(0.28f, 0.72f, 0.42f, 1.0f);   // 开启时的轨道颜色
        glm::vec4 offColor = glm::vec4(0.32f, 0.34f, 0.40f, 1.0f);  // 关闭时的轨道颜色
        glm::vec4 knobColor = glm::vec4(0.95f, 0.97f, 1.0f, 1.0f);  // 滑块颜色
    };

    // 进度条元素 —— 显示 [minValue, maxValue] 范围内的当前进度。
    class UIProgressBar : public UIElement {
    public:
        explicit UIProgressBar(std::string elementName = "ProgressBar");

    public:
        float minValue = 0.0f;         // 最小值
        float maxValue = 1.0f;         // 最大值
        float value = 0.65f;           // 当前值
        bool showPercentage = true;    // 是否在条上叠加百分比文字
        glm::vec4 fillColor = glm::vec4(0.35f, 0.72f, 0.96f, 1.0f); // 填充颜色


    protected:
        UIProgressBar(UIElementType elementType, std::string elementName);
    };

    class UIRadialProgressBar final : public UIProgressBar {
    public:
        explicit UIRadialProgressBar(std::string elementName = "RadialProgressBar");

    public:
        float startAngleDegrees = 135.0f;
        float sweepAngleDegrees = 270.0f;
        float outerRadiusRatio = 1.0f;
        float innerRadiusRatio = 0.72f;
        bool clockwise = true;
        bool tintBackgroundImage = false;
        bool tintFillImage = false;
        glm::vec4 backgroundFillColor = glm::vec4(0.18f, 0.22f, 0.30f, 1.0f);
        std::string backgroundImagePath;
        std::string fillImagePath;
    };

    // 输入框元素 —— 支持文本输入的可交互控件。
    class UIInputField final : public UIElement {
    public:
        explicit UIInputField(std::string elementName = "InputField", std::string fieldText = {});

    public:
        std::string text;                           // 当前输入的文本
        std::string placeholder = "Enter text...";  // 无输入时显示的占位文字
        bool readOnly = false;                       // 是否只读
        bool password = false;                       // 是否以密码模式显示（遮掩字符）
    };

} // namespace engine
