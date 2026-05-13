#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

#include <glm/glm.hpp>

#include "UICommon.hpp"

#ifdef None
#undef None
#endif

namespace engine {

    // 数据绑定系统支持的值类型枚举。
    enum class UIValueType : std::uint8_t {
        None = 0,   // 无效 / 未初始化
        Bool,       // 布尔值
        Int,        // 整数
        Float,      // 浮点数
        String,     // 字符串
        Vec2,       // 二维向量
        Color       // 四分量颜色 (RGBA)
    };

    // 数据绑定的目标属性 —— 指定数据上下文的值写入元素的哪个属性。
    enum class UIBindingTargetProperty : std::uint8_t {
        TextText = 0,       // Text 元素的文本内容
        ProgressBarValue,   // ProgressBar 的当前值
        SliderValue,        // Slider 的当前值
        ImageTintColor,     // Image 的染色
        ElementVisible,     // 元素可见性
        ElementOpacity,     // 元素不透明度
        ElementPosition,    // 元素位置
        ElementRotation     // 元素旋转角度
    };

    // 绑定更新策略。
    enum class UIBindingUpdateMode : std::uint8_t {
        EveryFrame = 0, // 每帧都从数据上下文读取并应用
        OnChange        // 仅当数据上下文中的值发生变化时才更新
    };

    // 数据绑定值的底层存储类型 —— 利用 std::variant 实现类型安全的多态值。
    using UIValueVariant = std::variant<std::monostate, bool, int, float, std::string, glm::vec2, glm::vec4>;

    struct UIValue;
    // 判断两个 UIValue 是否语义相等（浮点使用近似比较）。
    inline bool AreUIValuesEqual(const UIValue& lhs, const UIValue& rhs);

    // 带类型标签的多态值，支持 bool / int / float / string / vec2 / color。
    struct UIValue {
        UIValueVariant data;

        UIValue() = default;
        UIValue(bool value) : data(value) {}
        UIValue(int value) : data(value) {}
        UIValue(float value) : data(value) {}
        UIValue(std::string value) : data(std::move(value)) {}
        UIValue(const char* value) : data(std::string(value ? value : "")) {}
        UIValue(const glm::vec2& value) : data(value) {}
        UIValue(const glm::vec4& value) : data(value) {}

        UIValueType GetType() const {
            switch (data.index()) {
            case 1: return UIValueType::Bool;
            case 2: return UIValueType::Int;
            case 3: return UIValueType::Float;
            case 4: return UIValueType::String;
            case 5: return UIValueType::Vec2;
            case 6: return UIValueType::Color;
            default: return UIValueType::None;
            }
        }

        bool IsNumeric() const {
            return std::holds_alternative<bool>(data) ||
                std::holds_alternative<int>(data) ||
                std::holds_alternative<float>(data);
        }
    };

    // 数据上下文 —— 存储运行时 UI 绑定所需的键值对。
    // 由游戏逻辑（如 RenderSystem::RefreshRuntimeUiDataContext）写入，
    // 由 UIManager 在每帧 / 变更时读取并推送到对应元素属性。
    class UIDataContext {
    public:
        void SetBool(const std::string& key, bool value) { SetValue(key, UIValue(value)); }
        void SetInt(const std::string& key, int value) { SetValue(key, UIValue(value)); }
        void SetFloat(const std::string& key, float value) { SetValue(key, UIValue(value)); }
        void SetString(const std::string& key, std::string value) { SetValue(key, UIValue(std::move(value))); }
        void SetVec2(const std::string& key, const glm::vec2& value) { SetValue(key, UIValue(value)); }
        void SetColor(const std::string& key, const glm::vec4& value) { SetValue(key, UIValue(value)); }

        const UIValue* GetValue(const std::string& key) const {
            const auto iterator = mValues.find(key);
            return iterator == mValues.end() ? nullptr : &iterator->second.value;
        }

        bool HasValue(const std::string& key) const {
            return mValues.contains(key);
        }

        std::uint64_t GetRevision(const std::string& key) const {
            const auto iterator = mValues.find(key);
            return iterator == mValues.end() ? 0 : iterator->second.revision;
        }

        void Clear() {
            mValues.clear();
            mNextRevision = 1;
        }

        // 调试用：返回当前数据上下文中所有键的快照（按字典序排序）。
        std::vector<std::string> CollectKeys() const {
            std::vector<std::string> keys;
            keys.reserve(mValues.size());
            for (const auto& [key, _] : mValues) {
                keys.push_back(key);
            }
            std::sort(keys.begin(), keys.end());
            return keys;
        }

        // 调试用：上下文当前键值对总数。
        std::size_t Size() const {
            return mValues.size();
        }

    private:
        // 内部条目：值 + 修订号。修订号用于 OnChange 模式下判断是否需要更新。
        struct Entry {
            UIValue value;
            std::uint64_t revision = 0;
        };

        // 写入值并推进修订号；若值未变则跳过（避免不必要的更新）。
        void SetValue(const std::string& key, UIValue value) {
            if (key.empty()) {
                return;
            }

            auto& entry = mValues[key];
            if (entry.revision != 0 && AreUIValuesEqual(entry.value, value)) {
                return;
            }

            entry.value = std::move(value);
            entry.revision = mNextRevision++;
        }

    private:
        std::unordered_map<std::string, Entry> mValues;
        std::uint64_t mNextRevision = 1;
    };

    // 单条属性绑定的配置 —— 描述 "数据上下文的哪个键" 绑到 "哪个元素的哪个属性"。
    struct UIPropertyBinding {
        UIElementId targetElementId = 0;   // 目标元素 ID
        UIBindingTargetProperty targetProperty = UIBindingTargetProperty::TextText; // 目标属性
        std::string sourceKey;              // 数据上下文中的键名
        std::string formatString;           // 格式化字符串（如 "{0:.0f} km/h"）
        bool hasMin = false;                // 是否启用最小值钳位
        bool hasMax = false;                // 是否启用最大值钳位
        float minValue = 0.0f;             // 钳位最小值
        float maxValue = 1.0f;             // 钳位最大值
        bool invert = false;               // 是否对布尔值取反
        UIBindingUpdateMode updateMode = UIBindingUpdateMode::EveryFrame; // 更新策略

        // 运行时缓存，不参与序列化。
        mutable bool warnedMissingSource = false;
        mutable bool warnedTypeMismatch = false;
        mutable bool warnedInvalidTarget = false;
        mutable std::uint64_t lastObservedRevision = 0;
        mutable std::optional<UIValue> lastAppliedValue;
    };

    // 值类型枚举 -> 字符串。
    inline std::string_view ToString(UIValueType type) {
        switch (type) {
        case UIValueType::Bool: return "Bool";
        case UIValueType::Int: return "Int";
        case UIValueType::Float: return "Float";
        case UIValueType::String: return "String";
        case UIValueType::Vec2: return "Vec2";
        case UIValueType::Color: return "Color";
        default: return "None";
        }
    }

    // 目标属性枚举 -> 字符串（与 JSON 序列化的键名一致）。
    inline std::string_view ToString(UIBindingTargetProperty property) {
        switch (property) {
        case UIBindingTargetProperty::TextText: return "Text.text";
        case UIBindingTargetProperty::ProgressBarValue: return "ProgressBar.value";
        case UIBindingTargetProperty::SliderValue: return "Slider.value";
        case UIBindingTargetProperty::ImageTintColor: return "Image.tintColor";
        case UIBindingTargetProperty::ElementVisible: return "Element.visible";
        case UIBindingTargetProperty::ElementOpacity: return "Element.opacity";
        case UIBindingTargetProperty::ElementPosition: return "Element.position";
        case UIBindingTargetProperty::ElementRotation: return "Element.rotation";
        default: return "Text.text";
        }
    }

    // 更新模式枚举 -> 字符串。
    inline std::string_view ToString(UIBindingUpdateMode mode) {
        switch (mode) {
        case UIBindingUpdateMode::OnChange: return "OnChange";
        case UIBindingUpdateMode::EveryFrame:
        default:
            return "EveryFrame";
        }
    }

    // 字符串 -> 目标属性枚举。匹配失败返回 false。
    inline bool TryParseUIBindingTargetProperty(std::string_view name, UIBindingTargetProperty& outProperty) {
        for (UIBindingTargetProperty property : {
            UIBindingTargetProperty::TextText,
            UIBindingTargetProperty::ProgressBarValue,
            UIBindingTargetProperty::SliderValue,
            UIBindingTargetProperty::ImageTintColor,
            UIBindingTargetProperty::ElementVisible,
            UIBindingTargetProperty::ElementOpacity,
            UIBindingTargetProperty::ElementPosition,
            UIBindingTargetProperty::ElementRotation }) {
            if (ToString(property) == name) {
                outProperty = property;
                return true;
            }
        }

        return false;
    }

    // 字符串 -> 更新模式枚举。匹配失败返回 false。
    inline bool TryParseUIBindingUpdateMode(std::string_view name, UIBindingUpdateMode& outMode) {
        for (UIBindingUpdateMode mode : { UIBindingUpdateMode::EveryFrame, UIBindingUpdateMode::OnChange }) {
            if (ToString(mode) == name) {
                outMode = mode;
                return true;
            }
        }

        return false;
    }

    // 比较两个 UIValue 是否语义相等。浮点数使用 epsilon = 0.0001f 近似比较。
    inline bool AreUIValuesEqual(const UIValue& lhs, const UIValue& rhs) {
        if (lhs.GetType() != rhs.GetType()) {
            return false;
        }

        auto nearlyEqual = [](float a, float b) {
            return std::abs(a - b) <= 0.0001f;
        };

        if (std::holds_alternative<std::monostate>(lhs.data)) {
            return true;
        }
        if (const auto* boolValue = std::get_if<bool>(&lhs.data)) {
            return *boolValue == std::get<bool>(rhs.data);
        }
        if (const auto* intValue = std::get_if<int>(&lhs.data)) {
            return *intValue == std::get<int>(rhs.data);
        }
        if (const auto* floatValue = std::get_if<float>(&lhs.data)) {
            return nearlyEqual(*floatValue, std::get<float>(rhs.data));
        }
        if (const auto* stringValue = std::get_if<std::string>(&lhs.data)) {
            return *stringValue == std::get<std::string>(rhs.data);
        }
        if (const auto* vec2Value = std::get_if<glm::vec2>(&lhs.data)) {
            const glm::vec2& rhsValue = std::get<glm::vec2>(rhs.data);
            return nearlyEqual(vec2Value->x, rhsValue.x) && nearlyEqual(vec2Value->y, rhsValue.y);
        }
        if (const auto* colorValue = std::get_if<glm::vec4>(&lhs.data)) {
            const glm::vec4& rhsValue = std::get<glm::vec4>(rhs.data);
            return nearlyEqual(colorValue->x, rhsValue.x) &&
                nearlyEqual(colorValue->y, rhsValue.y) &&
                nearlyEqual(colorValue->z, rhsValue.z) &&
                nearlyEqual(colorValue->w, rhsValue.w);
        }

        return false;
    }

} // namespace engine
