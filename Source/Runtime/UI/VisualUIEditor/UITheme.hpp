#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "UIElement.hpp"

namespace engine {

    // 按钮主题样式，定义按钮在不同交互状态下的颜色、缩放和过渡动画参数
    struct UIButtonThemeStyle {
        bool enabled = false;
        UIButtonTransitionMode transitionMode = UIButtonTransitionMode::Animation;
        glm::vec4 normalColor = glm::vec4(0.24f, 0.31f, 0.55f, 1.0f);
        glm::vec4 hoverColor = glm::vec4(0.32f, 0.40f, 0.66f, 1.0f);
        glm::vec4 pressedColor = glm::vec4(0.18f, 0.24f, 0.42f, 1.0f);
        glm::vec4 disabledColor = glm::vec4(0.25f, 0.25f, 0.28f, 0.8f);
        float normalScale = 1.0f;
        float hoverScale = 1.04f;
        float pressedScale = 0.96f;
        float transitionDuration = 0.12f;
    };

    // UI样式预设，将通用样式和按钮样式组合为一个可复用的命名预设
    struct UIStylePreset {
        std::string name;
        UIStyle style;
        UIButtonThemeStyle buttonStyle;
    };

    // UI主题，从JSON主题文件（如 BicycleSim_DarkTheme.ui.theme.json）加载，包含颜色表、字体表和样式预设列表
    struct UITheme {
        std::string name;                                       // 主题名称
        std::unordered_map<std::string, glm::vec4> colors;      // 命名颜色映射表
        std::unordered_map<std::string, std::string> fonts;     // 命名字体路径映射表
        std::vector<UIStylePreset> presets;                      // 样式预设列表
    };

    // 解析后的按钮样式，合并主题预设和本地覆盖后的最终按钮交互样式
    struct ResolvedUIButtonStyle {
        UIButtonTransitionMode transitionMode = UIButtonTransitionMode::Animation;
        glm::vec4 normalColor = glm::vec4(0.24f, 0.31f, 0.55f, 1.0f);
        glm::vec4 hoverColor = glm::vec4(0.32f, 0.40f, 0.66f, 1.0f);
        glm::vec4 pressedColor = glm::vec4(0.18f, 0.24f, 0.42f, 1.0f);
        glm::vec4 disabledColor = glm::vec4(0.25f, 0.25f, 0.28f, 0.8f);
        float normalScale = 1.0f;
        float hoverScale = 1.04f;
        float pressedScale = 0.96f;
        float transitionDuration = 0.12f;
    };

    // 在主题中按名称查找样式预设，未找到返回nullptr
    const UIStylePreset* FindStylePreset(const UITheme& theme, std::string_view presetName);
    // 从JSON文件加载主题，带文件修改时间缓存，避免重复读取
    std::shared_ptr<const UITheme> LoadUiTheme(const std::filesystem::path& path);
    // 将主题序列化并保存到JSON文件
    bool SaveUiTheme(const UITheme& theme, const std::filesystem::path& path);
    // 使指定路径的主题缓存失效，下次加载时强制重新读取文件
    void InvalidateUiTheme(const std::filesystem::path& path);

    // 解析UI样式：以预设为基础，用本地覆盖标志选择性替换属性
    UIStyle ResolveStyle(const UIStyle& localStyle, const UITheme* theme);
    // 解析按钮样式：根据元素预设和按钮配置合并最终交互样式
    ResolvedUIButtonStyle ResolveButtonStyle(const UIElement& element, const UIButton& button, const UITheme* theme);

} // namespace engine
