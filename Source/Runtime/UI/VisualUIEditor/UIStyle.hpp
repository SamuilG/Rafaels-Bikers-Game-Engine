#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "UICommon.hpp"

namespace engine {

    // 按钮交互状态切换时的过渡效果类型。
    enum class UIButtonTransitionMode : std::uint8_t {
        None = 0,       // 无过渡效果
        ColorTint,      // 颜色色调过渡
        Scale,          // 缩放过渡
        Animation       // 自定义动画过渡
    };

    // 将按钮过渡模式转为可序列化的字符串。
    std::string_view ToString(UIButtonTransitionMode mode);
    // 从字符串解析按钮过渡模式，解析失败返回 false。
    bool TryParseUIButtonTransitionMode(std::string_view name, UIButtonTransitionMode& outMode);

    // 标记 UIStyle 中哪些字段由本地值覆盖主题预设。
    // true 表示使用元素自身的值，false 表示使用主题预设的值。
    struct UIStyleOverrideFlags {
        bool backgroundColor = true;
        bool tintColor = true;
        bool textColor = true;
        bool texturePath = true;
        bool fontPath = true;
        bool fontSize = true;
        bool opacity = true;
        bool padding = true;
        bool margin = true;
        bool borderColor = true;
        bool borderWidth = true;
        bool borderRadius = true;
    };

    // UI 元素的通用样式数据。
    // presetName 为空时直接使用本地字段；
    // presetName 非空时，会先取主题预设，再按 overrides 覆盖本地字段。
    struct UIStyle {
        std::string presetName;
        UIStyleOverrideFlags overrides;
        glm::vec4 backgroundColor = glm::vec4(0.0f);
        glm::vec4 tintColor = glm::vec4(1.0f);
        glm::vec4 textColor = glm::vec4(1.0f);
        std::string texturePath;
        std::string fontPath;
        float fontSize = 16.0f;
        float opacity = 1.0f;
        UISpacing padding;
        UISpacing margin;
        glm::vec4 borderColor = glm::vec4(0.0f);
        float borderWidth = 0.0f;
        float borderRadius = 0.0f;
    };

    // 批量设置样式所有覆盖标志位，enabled=true 表示全部使用本地值。
    inline void SetAllStyleOverrides(UIStyle& style, bool enabled) {
        style.overrides.backgroundColor = enabled;
        style.overrides.tintColor = enabled;
        style.overrides.textColor = enabled;
        style.overrides.texturePath = enabled;
        style.overrides.fontPath = enabled;
        style.overrides.fontSize = enabled;
        style.overrides.opacity = enabled;
        style.overrides.padding = enabled;
        style.overrides.margin = enabled;
        style.overrides.borderColor = enabled;
        style.overrides.borderWidth = enabled;
        style.overrides.borderRadius = enabled;
    }

} // namespace engine
