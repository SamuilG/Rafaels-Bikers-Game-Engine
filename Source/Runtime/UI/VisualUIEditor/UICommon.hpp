#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include <glm/glm.hpp>

namespace engine {

    // UI 系统里所有元素共享的稳定唯一标识。
    using UIElementId = std::uint64_t;

    // 运行时 UI / 编辑器 UI 共用的元素类型枚举。
    enum class UIElementType : std::uint8_t {
        Canvas = 0,
        Panel,
        Image,
        Text,
        Button,
        Slider,
        Toggle,
        ProgressBar,
        RadialProgressBar,
        ScrollView,
        InputField,
        HorizontalLayout,
        VerticalLayout,
        GridLayout
    };

    // 通用四边距结构，同时用于 padding 和 margin。
    struct UISpacing {
        float left = 0.0f;
        float top = 0.0f;
        float right = 0.0f;
        float bottom = 0.0f;
    };

    // 事件名占位结构。
    // 运行时真正的逻辑绑定由 UIManager / GameUIEventRouter 完成，
    // 这里仅保存配置层的事件名字符串。
    struct UIEventPlaceholders {
        std::string onClick;
        std::string onHover;
        std::string onPressed;
        std::string onReleased;
        std::string onValueChanged;
    };

    // 运行时和编辑器预览共用的轴对齐矩形描述。
    struct UIRect {
        glm::vec2 position = glm::vec2(0.0f);
        glm::vec2 size = glm::vec2(0.0f);
        float rotation = 0.0f;

        glm::vec2 Min() const {
            return position;
        }

        glm::vec2 Max() const {
            return position + size;
        }
    };

    // 将枚举类型转换为可序列化 / 可显示的字符串。
    std::string_view ToString(UIElementType type);
    // 在从磁盘反序列化时，把全局 ID 生成器推进到安全位置，避免新建元素复用旧 ID。
    void ReserveUIElementId(UIElementId usedId);

} // namespace engine
