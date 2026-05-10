#include "UITheme.hpp"

#include <algorithm>
#include <fstream>
#include <unordered_map>

#include "../../../../ThirdParty/json/json.hpp"

namespace engine {

    // 匿名命名空间：主题文件序列化/反序列化的内部实现
    namespace {

        using Json = nlohmann::ordered_json;
        constexpr int kUiThemeFileVersion = 1;   // 主题文件格式版本号

        // 主题缓存条目，记录文件修改时间以检测变更
        struct CachedThemeEntry {
            std::filesystem::file_time_type writeTime{};
            std::shared_ptr<const UITheme> theme;
        };

        // 获取全局主题缓存（静态局部变量，延迟初始化）
        std::unordered_map<std::string, CachedThemeEntry>& GetThemeCache() {
            static std::unordered_map<std::string, CachedThemeEntry> cache;
            return cache;
        }

        // 检查文件路径是否以 ".ui.theme.json" 扩展名结尾
        bool HasThemeJsonExtension(const std::filesystem::path& path) {
            const std::string filename = path.filename().string();
            return filename.size() >= 14 && filename.ends_with(".ui.theme.json");
        }

        // 将 glm::vec4 序列化为JSON对象 {x, y, z, w}
        Json SerializeVec4(const glm::vec4& value) {
            return Json{
                { "x", value.x },
                { "y", value.y },
                { "z", value.z },
                { "w", value.w }
            };
        }

        // 从JSON对象反序列化为 glm::vec4，缺失字段使用 fallback 默认值
        glm::vec4 DeserializeVec4(const Json& value, const glm::vec4& fallback = glm::vec4(0.0f)) {
            return glm::vec4(
                value.value("x", fallback.x),
                value.value("y", fallback.y),
                value.value("z", fallback.z),
                value.value("w", fallback.w));
        }

        // 将 UISpacing（上下左右间距）序列化为JSON对象
        Json SerializeSpacing(const UISpacing& value) {
            return Json{
                { "left", value.left },
                { "top", value.top },
                { "right", value.right },
                { "bottom", value.bottom }
            };
        }

        // 从JSON对象反序列化为 UISpacing，缺失字段使用 fallback 默认值
        UISpacing DeserializeSpacing(const Json& value, const UISpacing& fallback = {}) {
            UISpacing spacing = fallback;
            spacing.left = value.value("left", spacing.left);
            spacing.top = value.value("top", spacing.top);
            spacing.right = value.value("right", spacing.right);
            spacing.bottom = value.value("bottom", spacing.bottom);
            return spacing;
        }

        // 将样式覆盖标志序列化为JSON，每个标志表示对应属性是否被本地覆盖
        Json SerializeStyleOverrides(const UIStyleOverrideFlags& overrides) {
            return Json{
                { "backgroundColor", overrides.backgroundColor },
                { "tintColor", overrides.tintColor },
                { "textColor", overrides.textColor },
                { "texturePath", overrides.texturePath },
                { "fontPath", overrides.fontPath },
                { "fontSize", overrides.fontSize },
                { "opacity", overrides.opacity },
                { "padding", overrides.padding },
                { "margin", overrides.margin },
                { "borderColor", overrides.borderColor },
                { "borderWidth", overrides.borderWidth },
                { "borderRadius", overrides.borderRadius }
            };
        }

        // 从JSON反序列化样式覆盖标志
        void DeserializeStyleOverrides(const Json& value, UIStyleOverrideFlags& overrides) {
            overrides.backgroundColor = value.value("backgroundColor", overrides.backgroundColor);
            overrides.tintColor = value.value("tintColor", overrides.tintColor);
            overrides.textColor = value.value("textColor", overrides.textColor);
            overrides.texturePath = value.value("texturePath", overrides.texturePath);
            overrides.fontPath = value.value("fontPath", overrides.fontPath);
            overrides.fontSize = value.value("fontSize", overrides.fontSize);
            overrides.opacity = value.value("opacity", overrides.opacity);
            overrides.padding = value.value("padding", overrides.padding);
            overrides.margin = value.value("margin", overrides.margin);
            overrides.borderColor = value.value("borderColor", overrides.borderColor);
            overrides.borderWidth = value.value("borderWidth", overrides.borderWidth);
            overrides.borderRadius = value.value("borderRadius", overrides.borderRadius);
        }

        // 将完整的UI样式序列化为JSON，包含预设名、覆盖标志和所有样式属性
        Json SerializeStyle(const UIStyle& style) {
            return Json{
                { "presetName", style.presetName },
                { "overrides", SerializeStyleOverrides(style.overrides) },
                { "backgroundColor", SerializeVec4(style.backgroundColor) },
                { "tintColor", SerializeVec4(style.tintColor) },
                { "textColor", SerializeVec4(style.textColor) },
                { "texturePath", style.texturePath },
                { "fontPath", style.fontPath },
                { "fontSize", style.fontSize },
                { "opacity", style.opacity },
                { "padding", SerializeSpacing(style.padding) },
                { "margin", SerializeSpacing(style.margin) },
                { "borderColor", SerializeVec4(style.borderColor) },
                { "borderWidth", style.borderWidth },
                { "borderRadius", style.borderRadius }
            };
        }

        // 从JSON反序列化完整的UI样式
        void DeserializeStyle(const Json& value, UIStyle& style) {
            style.presetName = value.value("presetName", style.presetName);
            if (const auto overridesValue = value.find("overrides"); overridesValue != value.end() && overridesValue->is_object()) {
                DeserializeStyleOverrides(*overridesValue, style.overrides);
            }
            style.backgroundColor = DeserializeVec4(value.value("backgroundColor", Json::object()), style.backgroundColor);
            style.tintColor = DeserializeVec4(value.value("tintColor", Json::object()), style.tintColor);
            style.textColor = DeserializeVec4(value.value("textColor", Json::object()), style.textColor);
            style.texturePath = value.value("texturePath", style.texturePath);
            style.fontPath = value.value("fontPath", style.fontPath);
            style.fontSize = value.value("fontSize", style.fontSize);
            style.opacity = value.value("opacity", style.opacity);
            style.padding = DeserializeSpacing(value.value("padding", Json::object()), style.padding);
            style.margin = DeserializeSpacing(value.value("margin", Json::object()), style.margin);
            style.borderColor = DeserializeVec4(value.value("borderColor", Json::object()), style.borderColor);
            style.borderWidth = value.value("borderWidth", style.borderWidth);
            style.borderRadius = value.value("borderRadius", style.borderRadius);
        }

        // 将按钮主题样式序列化为JSON
        Json SerializeButtonThemeStyle(const UIButtonThemeStyle& style) {
            return Json{
                { "enabled", style.enabled },
                { "transitionMode", ToString(style.transitionMode) },
                { "normalColor", SerializeVec4(style.normalColor) },
                { "hoverColor", SerializeVec4(style.hoverColor) },
                { "pressedColor", SerializeVec4(style.pressedColor) },
                { "disabledColor", SerializeVec4(style.disabledColor) },
                { "normalScale", style.normalScale },
                { "hoverScale", style.hoverScale },
                { "pressedScale", style.pressedScale },
                { "transitionDuration", style.transitionDuration }
            };
        }

        // 从JSON反序列化按钮主题样式
        void DeserializeButtonThemeStyle(const Json& value, UIButtonThemeStyle& style) {
            style.enabled = value.value("enabled", style.enabled);
            TryParseUIButtonTransitionMode(
                value.value("transitionMode", std::string(ToString(style.transitionMode))),
                style.transitionMode);
            style.normalColor = DeserializeVec4(value.value("normalColor", Json::object()), style.normalColor);
            style.hoverColor = DeserializeVec4(value.value("hoverColor", Json::object()), style.hoverColor);
            style.pressedColor = DeserializeVec4(value.value("pressedColor", Json::object()), style.pressedColor);
            style.disabledColor = DeserializeVec4(value.value("disabledColor", Json::object()), style.disabledColor);
            style.normalScale = value.value("normalScale", style.normalScale);
            style.hoverScale = value.value("hoverScale", style.hoverScale);
            style.pressedScale = value.value("pressedScale", style.pressedScale);
            style.transitionDuration = value.value("transitionDuration", style.transitionDuration);
        }

        // 将样式预设序列化为JSON，包含名称、通用样式和按钮样式
        Json SerializePreset(const UIStylePreset& preset) {
            return Json{
                { "name", preset.name },
                { "style", SerializeStyle(preset.style) },
                { "buttonStyle", SerializeButtonThemeStyle(preset.buttonStyle) }
            };
        }

        // 从JSON反序列化样式预设
        UIStylePreset DeserializePreset(const Json& value) {
            UIStylePreset preset{};
            preset.name = value.value("name", preset.name);
            if (const auto styleValue = value.find("style"); styleValue != value.end() && styleValue->is_object()) {
                DeserializeStyle(*styleValue, preset.style);
            }
            if (const auto buttonValue = value.find("buttonStyle"); buttonValue != value.end() && buttonValue->is_object()) {
                DeserializeButtonThemeStyle(*buttonValue, preset.buttonStyle);
            }
            return preset;
        }

        // 将路径规范化为统一的缓存键，消除相对路径差异
        std::string NormalizeCacheKey(const std::filesystem::path& path) {
            std::error_code errorCode;
            const std::filesystem::path canonicalPath = std::filesystem::weakly_canonical(path, errorCode);
            return errorCode ? path.generic_string() : canonicalPath.generic_string();
        }

    } // namespace

    const UIStylePreset* FindStylePreset(const UITheme& theme, std::string_view presetName) {
        const auto iterator = std::find_if(theme.presets.begin(), theme.presets.end(), [&](const UIStylePreset& preset) {
            return preset.name == presetName;
        });
        return iterator == theme.presets.end() ? nullptr : &(*iterator);
    }

    std::shared_ptr<const UITheme> LoadUiTheme(const std::filesystem::path& path) {
        if (path.empty() || !HasThemeJsonExtension(path) || !std::filesystem::exists(path)) {
            return nullptr;
        }

        std::error_code errorCode;
        const auto writeTime = std::filesystem::last_write_time(path, errorCode);
        const std::string cacheKey = NormalizeCacheKey(path);
        auto& cache = GetThemeCache();
        if (!errorCode) {
            if (const auto iterator = cache.find(cacheKey); iterator != cache.end() && iterator->second.writeTime == writeTime) {
                return iterator->second.theme;
            }
        }

        std::ifstream input(path, std::ios::binary);
        if (!input) {
            return nullptr;
        }

        Json document;
        try {
            input >> document;
        }
        catch (...) {
            return nullptr;
        }

        if (!document.is_object() || document.value("version", -1) != kUiThemeFileVersion) {
            return nullptr;
        }

        auto theme = std::make_shared<UITheme>();
        theme->name = document.value("themeName", std::string("UITheme"));

        if (const auto colors = document.find("colors"); colors != document.end() && colors->is_object()) {
            for (auto iterator = colors->begin(); iterator != colors->end(); ++iterator) {
                theme->colors.emplace(iterator.key(), DeserializeVec4(iterator.value(), glm::vec4(1.0f)));
            }
        }

        if (const auto fonts = document.find("fonts"); fonts != document.end() && fonts->is_object()) {
            for (auto iterator = fonts->begin(); iterator != fonts->end(); ++iterator) {
                theme->fonts.emplace(iterator.key(), iterator.value().get<std::string>());
            }
        }

        if (const auto presets = document.find("presets"); presets != document.end() && presets->is_array()) {
            for (const Json& presetValue : *presets) {
                theme->presets.push_back(DeserializePreset(presetValue));
            }
        }

        cache[cacheKey] = CachedThemeEntry{ writeTime, theme };
        return theme;
    }

    bool SaveUiTheme(const UITheme& theme, const std::filesystem::path& path) {
        if (path.empty() || !HasThemeJsonExtension(path)) {
            return false;
        }

        std::error_code errorCode;
        const std::filesystem::path parentPath = path.parent_path();
        if (!parentPath.empty()) {
            std::filesystem::create_directories(parentPath, errorCode);
            if (errorCode) {
                return false;
            }
        }

        Json colors = Json::object();
        for (const auto& [key, value] : theme.colors) {
            colors[key] = SerializeVec4(value);
        }

        Json fonts = Json::object();
        for (const auto& [key, value] : theme.fonts) {
            fonts[key] = value;
        }

        Json presets = Json::array();
        for (const UIStylePreset& preset : theme.presets) {
            presets.push_back(SerializePreset(preset));
        }

        Json document{
            { "version", kUiThemeFileVersion },
            { "themeName", theme.name },
            { "colors", std::move(colors) },
            { "fonts", std::move(fonts) },
            { "presets", std::move(presets) }
        };

        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        if (!output) {
            return false;
        }

        output << document.dump(4);
        if (!output) {
            return false;
        }

        InvalidateUiTheme(path);
        return true;
    }

    void InvalidateUiTheme(const std::filesystem::path& path) {
        GetThemeCache().erase(NormalizeCacheKey(path));
    }

    UIStyle ResolveStyle(const UIStyle& localStyle, const UITheme* theme) {
        if (localStyle.presetName.empty() || theme == nullptr) {
            return localStyle;
        }

        const UIStylePreset* preset = FindStylePreset(*theme, localStyle.presetName);
        if (preset == nullptr) {
            return localStyle;
        }

        UIStyle resolved = preset->style;
        resolved.presetName = localStyle.presetName;
        resolved.overrides = localStyle.overrides;

        if (localStyle.overrides.backgroundColor) {
            resolved.backgroundColor = localStyle.backgroundColor;
        }
        if (localStyle.overrides.tintColor) {
            resolved.tintColor = localStyle.tintColor;
        }
        if (localStyle.overrides.textColor) {
            resolved.textColor = localStyle.textColor;
        }
        if (localStyle.overrides.texturePath) {
            resolved.texturePath = localStyle.texturePath;
        }
        if (localStyle.overrides.fontPath) {
            resolved.fontPath = localStyle.fontPath;
        }
        if (localStyle.overrides.fontSize) {
            resolved.fontSize = localStyle.fontSize;
        }
        if (localStyle.overrides.opacity) {
            resolved.opacity = localStyle.opacity;
        }
        if (localStyle.overrides.padding) {
            resolved.padding = localStyle.padding;
        }
        if (localStyle.overrides.margin) {
            resolved.margin = localStyle.margin;
        }
        if (localStyle.overrides.borderColor) {
            resolved.borderColor = localStyle.borderColor;
        }
        if (localStyle.overrides.borderWidth) {
            resolved.borderWidth = localStyle.borderWidth;
        }
        if (localStyle.overrides.borderRadius) {
            resolved.borderRadius = localStyle.borderRadius;
        }

        return resolved;
    }

    ResolvedUIButtonStyle ResolveButtonStyle(const UIElement& element, const UIButton& button, const UITheme* theme) {
        ResolvedUIButtonStyle resolved{};
        const UIStyle resolvedElementStyle = ResolveStyle(element.style, theme);
        resolved.transitionMode = button.transitionMode;
        resolved.normalColor = resolvedElementStyle.backgroundColor;
        resolved.hoverColor = button.hoverColor;
        resolved.pressedColor = button.pressedColor;
        resolved.disabledColor = button.disabledColor;
        resolved.normalScale = button.normalScale;
        resolved.hoverScale = button.hoverScale;
        resolved.pressedScale = button.pressedScale;
        resolved.transitionDuration = button.transitionDuration;

        if (element.style.presetName.empty() || theme == nullptr || !button.usePresetTransitionStyle) {
            return resolved;
        }

        const UIStylePreset* preset = FindStylePreset(*theme, element.style.presetName);
        if (preset == nullptr || !preset->buttonStyle.enabled) {
            return resolved;
        }

        resolved.transitionMode = preset->buttonStyle.transitionMode;
        resolved.normalColor = preset->buttonStyle.normalColor;
        resolved.hoverColor = preset->buttonStyle.hoverColor;
        resolved.pressedColor = preset->buttonStyle.pressedColor;
        resolved.disabledColor = preset->buttonStyle.disabledColor;
        resolved.normalScale = preset->buttonStyle.normalScale;
        resolved.hoverScale = preset->buttonStyle.hoverScale;
        resolved.pressedScale = preset->buttonStyle.pressedScale;
        resolved.transitionDuration = preset->buttonStyle.transitionDuration;

        if (element.style.overrides.backgroundColor) {
            resolved.normalColor = element.style.backgroundColor;
        }
        return resolved;
    }

} // namespace engine
