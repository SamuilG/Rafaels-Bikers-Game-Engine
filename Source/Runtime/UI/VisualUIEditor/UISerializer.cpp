#include "UISerializer.hpp"

#include <fstream>
#include <utility>

#include "../../../../ThirdParty/json/json.hpp"

namespace engine {

    // 匿名命名空间：所有序列化/反序列化的内部辅助函数
    namespace {

        using Json = nlohmann::ordered_json;
        // UI 文件格式版本号，用于前向兼容校验
        constexpr int kUiFileVersion = 1;

        // 检查文件路径是否以 ".ui.json" 结尾
        bool HasUiJsonExtension(const std::filesystem::path& path) {
            const std::string filename = path.filename().string();
            return filename.size() >= 8 && filename.ends_with(".ui.json");
        }

        // ---------- 基础类型序列化/反序列化 ----------

        // 将 vec2 序列化为 JSON 对象 {x, y}
        Json SerializeVec2(const glm::vec2& value) {
            return Json{
                { "x", value.x },
                { "y", value.y }
            };
        }

        // 将 vec4 序列化为 JSON 对象 {x, y, z, w}
        Json SerializeVec4(const glm::vec4& value) {
            return Json{
                { "x", value.x },
                { "y", value.y },
                { "z", value.z },
                { "w", value.w }
            };
        }

        // 将边距数据序列化为 JSON 对象 {left, top, right, bottom}
        Json SerializeSpacing(const UISpacing& value) {
            return Json{
                { "left", value.left },
                { "top", value.top },
                { "right", value.right },
                { "bottom", value.bottom }
            };
        }

        // 从 JSON 反序列化 vec2，缺失字段使用 fallback 默认值
        glm::vec2 DeserializeVec2(const Json& value, const glm::vec2& fallback = glm::vec2(0.0f)) {
            return glm::vec2(
                value.value("x", fallback.x),
                value.value("y", fallback.y)
            );
        }

        // 从 JSON 反序列化 vec4，缺失字段使用 fallback 默认值
        glm::vec4 DeserializeVec4(const Json& value, const glm::vec4& fallback = glm::vec4(0.0f)) {
            return glm::vec4(
                value.value("x", fallback.x),
                value.value("y", fallback.y),
                value.value("z", fallback.z),
                value.value("w", fallback.w)
            );
        }

        // 从 JSON 反序列化边距数据
        UISpacing DeserializeSpacing(const Json& value, const UISpacing& fallback = {}) {
            UISpacing spacing = fallback;
            spacing.left = value.value("left", spacing.left);
            spacing.top = value.value("top", spacing.top);
            spacing.right = value.value("right", spacing.right);
            spacing.bottom = value.value("bottom", spacing.bottom);
            return spacing;
        }

        // ---------- 屏幕设置序列化/反序列化 ----------

        // 序列化屏幕级别设置（可见性、输入阻断、渲染顺序、过渡动画等）
        Json SerializeScreenSettings(const UIScreen& screen) {
            return Json{
                { "visible", screen.IsVisible() },
                { "blocksInput", screen.BlocksInput() },
                { "renderOrder", screen.GetRenderOrder() },
                { "pauseGameWhenOpen", screen.PauseGameWhenOpen() },
                { "allowInputPassthrough", screen.AllowInputPassthrough() },
                { "enterAnimation", screen.GetEnterAnimation() },
                { "exitAnimation", screen.GetExitAnimation() },
                { "hideAfterExit", screen.HideAfterExit() },
                { "blockInputDuringTransition", screen.BlockInputDuringTransition() }
            };
        }

        // 从 JSON 恢复屏幕级别设置到 UIScreen 对象
        void DeserializeScreenSettings(const Json& value, UIScreen& screen) {
            screen.SetVisible(value.value("visible", screen.IsVisible()));
            screen.SetBlocksInput(value.value("blocksInput", screen.BlocksInput()));
            screen.SetRenderOrder(value.value("renderOrder", screen.GetRenderOrder()));
            screen.SetPauseGameWhenOpen(value.value("pauseGameWhenOpen", screen.PauseGameWhenOpen()));
            screen.SetAllowInputPassthrough(value.value("allowInputPassthrough", screen.AllowInputPassthrough()));
            screen.SetEnterAnimation(value.value("enterAnimation", screen.GetEnterAnimation()));
            screen.SetExitAnimation(value.value("exitAnimation", screen.GetExitAnimation()));
            screen.SetHideAfterExit(value.value("hideAfterExit", screen.HideAfterExit()));
            screen.SetBlockInputDuringTransition(value.value("blockInputDuringTransition", screen.BlockInputDuringTransition()));
        }

        // ---------- 属性绑定序列化/反序列化 ----------

        // 序列化单个属性绑定（目标元素、属性名、数据源键、格式字符串、范围限制等）
        Json SerializeBinding(const UIPropertyBinding& binding) {
            Json result{
                { "targetElementId", binding.targetElementId },
                { "targetProperty", ToString(binding.targetProperty) },
                { "sourceKey", binding.sourceKey },
                { "formatString", binding.formatString },
                { "invert", binding.invert },
                { "updateMode", ToString(binding.updateMode) }
            };
            if (binding.hasMin) {
                result["min"] = binding.minValue;
            }
            if (binding.hasMax) {
                result["max"] = binding.maxValue;
            }
            return result;
        }

        // 从 JSON 反序列化属性绑定，解析目标属性和更新模式枚举
        UIPropertyBinding DeserializeBinding(const Json& value) {
            UIPropertyBinding binding{};
            binding.targetElementId = value.value("targetElementId", binding.targetElementId);
            binding.sourceKey = value.value("sourceKey", binding.sourceKey);
            binding.formatString = value.value("formatString", binding.formatString);
            binding.invert = value.value("invert", binding.invert);
            binding.hasMin = value.contains("min");
            binding.hasMax = value.contains("max");
            binding.minValue = value.value("min", binding.minValue);
            binding.maxValue = value.value("max", binding.maxValue);

            UIBindingTargetProperty targetProperty = binding.targetProperty;
            if (TryParseUIBindingTargetProperty(
                value.value("targetProperty", std::string(ToString(binding.targetProperty))),
                targetProperty)) {
                binding.targetProperty = targetProperty;
            }

            UIBindingUpdateMode updateMode = binding.updateMode;
            if (TryParseUIBindingUpdateMode(
                value.value("updateMode", std::string(ToString(binding.updateMode))),
                updateMode)) {
                binding.updateMode = updateMode;
            }

            return binding;
        }

        // ---------- UIValue 多态值序列化/反序列化 ----------

        // 序列化 UIValue（variant 类型），根据实际存储类型输出对应 JSON
        Json SerializeUiValue(const UIValue& value) {
            Json result{
                { "type", ToString(value.GetType()) }
            };

            if (const auto* boolValue = std::get_if<bool>(&value.data)) {
                result["value"] = *boolValue;
            }
            else if (const auto* intValue = std::get_if<int>(&value.data)) {
                result["value"] = *intValue;
            }
            else if (const auto* floatValue = std::get_if<float>(&value.data)) {
                result["value"] = *floatValue;
            }
            else if (const auto* stringValue = std::get_if<std::string>(&value.data)) {
                result["value"] = *stringValue;
            }
            else if (const auto* vec2Value = std::get_if<glm::vec2>(&value.data)) {
                result["value"] = SerializeVec2(*vec2Value);
            }
            else if (const auto* colorValue = std::get_if<glm::vec4>(&value.data)) {
                result["value"] = SerializeVec4(*colorValue);
            }
            else {
                result["value"] = Json{};
            }

            return result;
        }

        // 根据 JSON 中的 type 字段还原对应类型的 UIValue
        UIValue DeserializeUiValue(const Json& value) {
            const std::string typeName = value.value("type", std::string("None"));
            const Json payload = value.value("value", Json{});
            if (typeName == ToString(UIValueType::Bool)) {
                return UIValue(payload.get<bool>());
            }
            if (typeName == ToString(UIValueType::Int)) {
                return UIValue(payload.get<int>());
            }
            if (typeName == ToString(UIValueType::Float)) {
                return UIValue(payload.get<float>());
            }
            if (typeName == ToString(UIValueType::String)) {
                return UIValue(payload.get<std::string>());
            }
            if (typeName == ToString(UIValueType::Vec2)) {
                return UIValue(DeserializeVec2(payload, glm::vec2(0.0f)));
            }
            if (typeName == ToString(UIValueType::Color)) {
                return UIValue(DeserializeVec4(payload, glm::vec4(1.0f)));
            }
            return UIValue{};
        }

        // ---------- 动画系统序列化/反序列化 ----------

        // 序列化单个关键帧（时间点、值、缓动函数）
        Json SerializeKeyframe(const UIKeyframe& keyframe) {
            return Json{
                { "time", keyframe.time },
                { "value", SerializeUiValue(keyframe.value) },
                { "easing", ToString(keyframe.easing) }
            };
        }

        // 从 JSON 反序列化关键帧
        UIKeyframe DeserializeKeyframe(const Json& value) {
            UIKeyframe keyframe{};
            keyframe.time = value.value("time", keyframe.time);
            keyframe.value = DeserializeUiValue(value.value("value", Json::object()));
            UIAnimationEasing easing = keyframe.easing;
            if (TryParseUIAnimationEasing(value.value("easing", std::string(ToString(keyframe.easing))), easing)) {
                keyframe.easing = easing;
            }
            return keyframe;
        }

        // 序列化动画轨道（目标元素、动画属性、关键帧列表）
        Json SerializeAnimationTrack(const UIAnimationTrack& track) {
            Json keyframes = Json::array();
            for (const UIKeyframe& keyframe : track.keyframes) {
                keyframes.push_back(SerializeKeyframe(keyframe));
            }

            return Json{
                { "targetElementId", track.targetElementId },
                { "property", ToString(track.property) },
                { "keyframes", std::move(keyframes) }
            };
        }

        UIAnimationTrack DeserializeAnimationTrack(const Json& value) {
            UIAnimationTrack track{};
            track.targetElementId = value.value("targetElementId", track.targetElementId);
            UIAnimationProperty property = track.property;
            if (TryParseUIAnimationProperty(value.value("property", std::string(ToString(track.property))), property)) {
                track.property = property;
            }
            if (const auto keyframes = value.find("keyframes"); keyframes != value.end() && keyframes->is_array()) {
                for (const Json& keyframeValue : *keyframes) {
                    track.keyframes.push_back(DeserializeKeyframe(keyframeValue));
                }
            }
            return track;
        }

        Json SerializeAnimationClip(const UIAnimationClip& clip) {
            Json tracks = Json::array();
            for (const UIAnimationTrack& track : clip.tracks) {
                tracks.push_back(SerializeAnimationTrack(track));
            }

            return Json{
                { "name", clip.name },
                { "duration", clip.duration },
                { "loopCount", clip.loopCount },
                { "playOnShow", clip.playOnShow },
                { "trackCount", clip.tracks.size() },
                { "tracks", std::move(tracks) }
            };
        }

        UIAnimationClip DeserializeAnimationClip(const Json& value) {
            UIAnimationClip clip{};
            clip.name = value.value("name", clip.name);
            clip.duration = value.value("duration", clip.duration);
            clip.loopCount = value.value("loopCount", clip.loopCount);
            clip.playOnShow = value.contains("playOnShow")
                ? value.value("playOnShow", clip.playOnShow)
                : (clip.name != "LowEnergy_Pulse");
            if (const auto tracks = value.find("tracks"); tracks != value.end() && tracks->is_array()) {
                for (const Json& trackValue : *tracks) {
                    clip.tracks.push_back(DeserializeAnimationTrack(trackValue));
                }
            }
            return clip;
        }

        Json SerializeTransform(const UITransform& transform) {
            return Json{
                { "position", SerializeVec2(transform.position) },
                { "size", SerializeVec2(transform.size) },
                { "anchorMin", SerializeVec2(transform.anchorMin) },
                { "anchorMax", SerializeVec2(transform.anchorMax) },
                { "pivot", SerializeVec2(transform.pivot) },
                { "rotation", transform.rotation },
                { "scale", SerializeVec2(transform.scale) }
            };
        }

        void DeserializeTransform(const Json& value, UITransform& transform) {
            transform.position = DeserializeVec2(value.value("position", Json::object()), transform.position);
            transform.size = DeserializeVec2(value.value("size", Json::object()), transform.size);
            transform.anchorMin = DeserializeVec2(value.value("anchorMin", Json::object()), transform.anchorMin);
            transform.anchorMax = DeserializeVec2(value.value("anchorMax", Json::object()), transform.anchorMax);
            transform.pivot = DeserializeVec2(value.value("pivot", Json::object()), transform.pivot);
            transform.rotation = value.value("rotation", transform.rotation);
            transform.scale = DeserializeVec2(value.value("scale", Json::object()), transform.scale);
        }

        Json SerializeStyle(const UIStyle& style) {
            return Json{
                { "presetName", style.presetName },
                { "overrides", Json{
                    { "backgroundColor", style.overrides.backgroundColor },
                    { "tintColor", style.overrides.tintColor },
                    { "textColor", style.overrides.textColor },
                    { "texturePath", style.overrides.texturePath },
                    { "fontPath", style.overrides.fontPath },
                    { "fontSize", style.overrides.fontSize },
                    { "opacity", style.overrides.opacity },
                    { "padding", style.overrides.padding },
                    { "margin", style.overrides.margin },
                    { "borderColor", style.overrides.borderColor },
                    { "borderWidth", style.overrides.borderWidth },
                    { "borderRadius", style.overrides.borderRadius }
                } },
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

        void DeserializeStyle(const Json& value, UIStyle& style) {
            style.presetName = value.value("presetName", style.presetName);
            if (const auto overridesValue = value.find("overrides"); overridesValue != value.end() && overridesValue->is_object()) {
                style.overrides.backgroundColor = overridesValue->value("backgroundColor", style.overrides.backgroundColor);
                style.overrides.tintColor = overridesValue->value("tintColor", style.overrides.tintColor);
                style.overrides.textColor = overridesValue->value("textColor", style.overrides.textColor);
                style.overrides.texturePath = overridesValue->value("texturePath", style.overrides.texturePath);
                style.overrides.fontPath = overridesValue->value("fontPath", style.overrides.fontPath);
                style.overrides.fontSize = overridesValue->value("fontSize", style.overrides.fontSize);
                style.overrides.opacity = overridesValue->value("opacity", style.overrides.opacity);
                style.overrides.padding = overridesValue->value("padding", style.overrides.padding);
                style.overrides.margin = overridesValue->value("margin", style.overrides.margin);
                style.overrides.borderColor = overridesValue->value("borderColor", style.overrides.borderColor);
                style.overrides.borderWidth = overridesValue->value("borderWidth", style.overrides.borderWidth);
                style.overrides.borderRadius = overridesValue->value("borderRadius", style.overrides.borderRadius);
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

        Json SerializeEvents(const UIEventPlaceholders& events) {
            return Json{
                { "onClick", events.onClick },
                { "onHover", events.onHover },
                { "onPressed", events.onPressed },
                { "onReleased", events.onReleased },
                { "onValueChanged", events.onValueChanged }
            };
        }

        void DeserializeEvents(const Json& value, UIEventPlaceholders& events) {
            events.onClick = value.value("onClick", events.onClick);
            events.onHover = value.value("onHover", events.onHover);
            events.onPressed = value.value("onPressed", events.onPressed);
            events.onReleased = value.value("onReleased", events.onReleased);
            events.onValueChanged = value.value("onValueChanged", events.onValueChanged);
        }

        Json SerializeWidgetData(const UIElement& element) {
            Json widgetData = Json::object();

            if (const auto* image = dynamic_cast<const UIImage*>(&element)) {
                widgetData["imagePath"] = image->imagePath;
                widgetData["preserveAspectRatio"] = image->preserveAspectRatio;
            }
            else if (const auto* text = dynamic_cast<const UIText*>(&element)) {
                widgetData["text"] = text->text;
                widgetData["alignment"] = text->alignment;
                widgetData["wrapText"] = text->wrapText;
            }
            else if (const auto* button = dynamic_cast<const UIButton*>(&element)) {
                widgetData["label"] = button->label;
                widgetData["pressed"] = button->pressed;
                widgetData["usePresetTransitionStyle"] = button->usePresetTransitionStyle;
                widgetData["transitionMode"] = ToString(button->transitionMode);
                widgetData["normalColor"] = SerializeVec4(button->normalColor);
                widgetData["hoverColor"] = SerializeVec4(button->hoverColor);
                widgetData["pressedColor"] = SerializeVec4(button->pressedColor);
                widgetData["disabledColor"] = SerializeVec4(button->disabledColor);
                widgetData["normalScale"] = button->normalScale;
                widgetData["hoverScale"] = button->hoverScale;
                widgetData["pressedScale"] = button->pressedScale;
                widgetData["transitionDuration"] = button->transitionDuration;
            }
            else if (const auto* slider = dynamic_cast<const UISlider*>(&element)) {
                widgetData["minValue"] = slider->minValue;
                widgetData["maxValue"] = slider->maxValue;
                widgetData["value"] = slider->value;
                widgetData["wholeNumbers"] = slider->wholeNumbers;
                widgetData["fillColor"] = SerializeVec4(slider->fillColor);
                widgetData["handleColor"] = SerializeVec4(slider->handleColor);
            }
            else if (const auto* toggle = dynamic_cast<const UIToggle*>(&element)) {
                widgetData["label"] = toggle->label;
                widgetData["isOn"] = toggle->isOn;
                widgetData["onColor"] = SerializeVec4(toggle->onColor);
                widgetData["offColor"] = SerializeVec4(toggle->offColor);
                widgetData["knobColor"] = SerializeVec4(toggle->knobColor);
            }
            else if (const auto* radialProgressBar = dynamic_cast<const UIRadialProgressBar*>(&element)) {
                widgetData["minValue"] = radialProgressBar->minValue;
                widgetData["maxValue"] = radialProgressBar->maxValue;
                widgetData["value"] = radialProgressBar->value;
                widgetData["showPercentage"] = radialProgressBar->showPercentage;
                widgetData["fillColor"] = SerializeVec4(radialProgressBar->fillColor);
                widgetData["startAngleDegrees"] = radialProgressBar->startAngleDegrees;
                widgetData["sweepAngleDegrees"] = radialProgressBar->sweepAngleDegrees;
                widgetData["outerRadiusRatio"] = radialProgressBar->outerRadiusRatio;
                widgetData["innerRadiusRatio"] = radialProgressBar->innerRadiusRatio;
                widgetData["clockwise"] = radialProgressBar->clockwise;
                widgetData["tintBackgroundImage"] = radialProgressBar->tintBackgroundImage;
                widgetData["tintFillImage"] = radialProgressBar->tintFillImage;
                widgetData["backgroundFillColor"] = SerializeVec4(radialProgressBar->backgroundFillColor);
                widgetData["backgroundImagePath"] = radialProgressBar->backgroundImagePath;
                widgetData["fillImagePath"] = radialProgressBar->fillImagePath;
            }
            else if (const auto* progressBar = dynamic_cast<const UIProgressBar*>(&element)) {
                widgetData["minValue"] = progressBar->minValue;
                widgetData["maxValue"] = progressBar->maxValue;
                widgetData["value"] = progressBar->value;
                widgetData["showPercentage"] = progressBar->showPercentage;
                widgetData["fillColor"] = SerializeVec4(progressBar->fillColor);
            }
            else if (const auto* inputField = dynamic_cast<const UIInputField*>(&element)) {
                widgetData["text"] = inputField->text;
                widgetData["placeholder"] = inputField->placeholder;
                widgetData["readOnly"] = inputField->readOnly;
                widgetData["password"] = inputField->password;
            }

            return widgetData;
        }

        std::unique_ptr<UIElement> CreateElementForType(UIElementType type, const std::string& name) {
            switch (type) {
            case UIElementType::Panel:
                return std::make_unique<UIPanel>(name);
            case UIElementType::Image:
                return std::make_unique<UIImage>(name);
            case UIElementType::Text:
                return std::make_unique<UIText>(name);
            case UIElementType::Button:
                return std::make_unique<UIButton>(name);
            case UIElementType::Slider:
                return std::make_unique<UISlider>(name);
            case UIElementType::Toggle:
                return std::make_unique<UIToggle>(name);
            case UIElementType::ProgressBar:
                return std::make_unique<UIProgressBar>(name);
            case UIElementType::RadialProgressBar:
                return std::make_unique<UIRadialProgressBar>(name);
            case UIElementType::InputField:
                return std::make_unique<UIInputField>(name);
            default:
                return std::make_unique<UIElement>(type, name);
            }
        }

        bool TryParseElementType(std::string_view name, UIElementType& outType) {
            for (UIElementType type : {
                UIElementType::Canvas,
                UIElementType::Panel,
                UIElementType::Image,
                UIElementType::Text,
                UIElementType::Button,
                UIElementType::Slider,
                UIElementType::Toggle,
                UIElementType::ProgressBar,
                UIElementType::RadialProgressBar,
                UIElementType::ScrollView,
                UIElementType::InputField,
                UIElementType::HorizontalLayout,
                UIElementType::VerticalLayout,
                UIElementType::GridLayout
                }) {
                if (ToString(type) == name) {
                    outType = type;
                    return true;
                }
            }

            return false;
        }

        Json SerializeElement(const UIElement& element) {
            Json children = Json::array();
            for (const auto& child : element.GetChildren()) {
                children.push_back(SerializeElement(*child));
            }

            return Json{
                { "id", element.id },
                { "name", element.name },
                { "type", ToString(element.type) },
                { "visible", element.visible },
                { "enabled", element.enabled },
                { "interactable", element.interactable },
                { "runtimeMutable", element.runtimeMutable },
                { "zOrder", element.zOrder },
                { "transform", SerializeTransform(element.transform) },
                { "style", SerializeStyle(element.style) },
                { "widgetData", SerializeWidgetData(element) },
                { "events", SerializeEvents(element.events) },
                { "children", std::move(children) }
            };
        }

        void DeserializeWidgetData(const Json& widgetData, UIElement& element) {
            if (auto* image = dynamic_cast<UIImage*>(&element)) {
                image->imagePath = widgetData.value("imagePath", image->imagePath);
                image->preserveAspectRatio = widgetData.value("preserveAspectRatio", image->preserveAspectRatio);
                element.style.texturePath = image->imagePath;
            }
            else if (auto* text = dynamic_cast<UIText*>(&element)) {
                text->text = widgetData.value("text", text->text);
                text->alignment = widgetData.value("alignment", text->alignment);
                text->wrapText = widgetData.value("wrapText", text->wrapText);
            }
            else if (auto* button = dynamic_cast<UIButton*>(&element)) {
                button->label = widgetData.value("label", button->label);
                button->pressed = widgetData.value("pressed", button->pressed);
                button->usePresetTransitionStyle = widgetData.value("usePresetTransitionStyle", button->usePresetTransitionStyle);
                const std::string transitionModeName = widgetData.value("transitionMode", std::string(ToString(button->transitionMode)));
                TryParseUIButtonTransitionMode(transitionModeName, button->transitionMode);
                button->normalColor = DeserializeVec4(widgetData.value("normalColor", Json::object()), button->normalColor);
                button->hoverColor = DeserializeVec4(widgetData.value("hoverColor", Json::object()), button->hoverColor);
                button->pressedColor = DeserializeVec4(widgetData.value("pressedColor", Json::object()), button->pressedColor);
                button->disabledColor = DeserializeVec4(widgetData.value("disabledColor", Json::object()), button->disabledColor);
                button->normalScale = widgetData.value("normalScale", button->normalScale);
                button->hoverScale = widgetData.value("hoverScale", button->hoverScale);
                button->pressedScale = widgetData.value("pressedScale", button->pressedScale);
                button->transitionDuration = widgetData.value("transitionDuration", button->transitionDuration);
                button->runtimeVisualColor = button->normalColor;
                button->runtimeVisualScale = button->normalScale;
                button->runtimeVisualInitialized = false;
            }
            else if (auto* slider = dynamic_cast<UISlider*>(&element)) {
                slider->minValue = widgetData.value("minValue", slider->minValue);
                slider->maxValue = widgetData.value("maxValue", slider->maxValue);
                slider->value = widgetData.value("value", slider->value);
                slider->wholeNumbers = widgetData.value("wholeNumbers", slider->wholeNumbers);
                slider->fillColor = DeserializeVec4(widgetData.value("fillColor", Json::object()), slider->fillColor);
                slider->handleColor = DeserializeVec4(widgetData.value("handleColor", Json::object()), slider->handleColor);
            }
            else if (auto* toggle = dynamic_cast<UIToggle*>(&element)) {
                toggle->label = widgetData.value("label", toggle->label);
                toggle->isOn = widgetData.value("isOn", toggle->isOn);
                toggle->onColor = DeserializeVec4(widgetData.value("onColor", Json::object()), toggle->onColor);
                toggle->offColor = DeserializeVec4(widgetData.value("offColor", Json::object()), toggle->offColor);
                toggle->knobColor = DeserializeVec4(widgetData.value("knobColor", Json::object()), toggle->knobColor);
            }
            else if (auto* radialProgressBar = dynamic_cast<UIRadialProgressBar*>(&element)) {
                radialProgressBar->minValue = widgetData.value("minValue", radialProgressBar->minValue);
                radialProgressBar->maxValue = widgetData.value("maxValue", radialProgressBar->maxValue);
                radialProgressBar->value = widgetData.value("value", radialProgressBar->value);
                radialProgressBar->showPercentage = widgetData.value("showPercentage", radialProgressBar->showPercentage);
                radialProgressBar->fillColor = DeserializeVec4(widgetData.value("fillColor", Json::object()), radialProgressBar->fillColor);
                radialProgressBar->startAngleDegrees = widgetData.value("startAngleDegrees", radialProgressBar->startAngleDegrees);
                radialProgressBar->sweepAngleDegrees = widgetData.value("sweepAngleDegrees", radialProgressBar->sweepAngleDegrees);
                radialProgressBar->outerRadiusRatio = widgetData.value("outerRadiusRatio", radialProgressBar->outerRadiusRatio);
                radialProgressBar->innerRadiusRatio = widgetData.value("innerRadiusRatio", radialProgressBar->innerRadiusRatio);
                radialProgressBar->clockwise = widgetData.value("clockwise", radialProgressBar->clockwise);
                radialProgressBar->tintBackgroundImage = widgetData.value("tintBackgroundImage", radialProgressBar->tintBackgroundImage);
                radialProgressBar->tintFillImage = widgetData.value("tintFillImage", radialProgressBar->tintFillImage);
                radialProgressBar->backgroundFillColor = DeserializeVec4(widgetData.value("backgroundFillColor", Json::object()), radialProgressBar->backgroundFillColor);
                radialProgressBar->backgroundImagePath = widgetData.value("backgroundImagePath", radialProgressBar->backgroundImagePath);
                radialProgressBar->fillImagePath = widgetData.value("fillImagePath", radialProgressBar->fillImagePath);
            }
            else if (auto* progressBar = dynamic_cast<UIProgressBar*>(&element)) {
                progressBar->minValue = widgetData.value("minValue", progressBar->minValue);
                progressBar->maxValue = widgetData.value("maxValue", progressBar->maxValue);
                progressBar->value = widgetData.value("value", progressBar->value);
                progressBar->showPercentage = widgetData.value("showPercentage", progressBar->showPercentage);
                progressBar->fillColor = DeserializeVec4(widgetData.value("fillColor", Json::object()), progressBar->fillColor);
            }
            else if (auto* inputField = dynamic_cast<UIInputField*>(&element)) {
                inputField->text = widgetData.value("text", inputField->text);
                inputField->placeholder = widgetData.value("placeholder", inputField->placeholder);
                inputField->readOnly = widgetData.value("readOnly", inputField->readOnly);
                inputField->password = widgetData.value("password", inputField->password);
            }
        }

        std::unique_ptr<UIElement> DeserializeElement(const Json& value) {
            UIElementType type = UIElementType::Panel;
            const std::string typeName = value.value("type", std::string(ToString(type)));
            if (!TryParseElementType(typeName, type)) {
                return nullptr;
            }

            auto element = CreateElementForType(type, value.value("name", std::string(ToString(type))));
            if (!element) {
                return nullptr;
            }

            element->id = value.value("id", element->id);
            ReserveUIElementId(element->id);
            element->visible = value.value("visible", element->visible);
            element->enabled = value.value("enabled", element->enabled);
            element->interactable = value.value("interactable", element->interactable);
            element->runtimeMutable = value.value("runtimeMutable", element->runtimeMutable);
            element->zOrder = value.value("zOrder", element->zOrder);

            DeserializeTransform(value.value("transform", Json::object()), element->transform);
            DeserializeStyle(value.value("style", Json::object()), element->style);
            DeserializeEvents(value.value("events", Json::object()), element->events);
            DeserializeWidgetData(value.value("widgetData", Json::object()), *element);

            if (value.contains("children") && value["children"].is_array()) {
                for (const auto& childValue : value["children"]) {
                    if (auto child = DeserializeElement(childValue)) {
                        element->AddChild(std::move(child));
                    }
                }
            }

            return element;
        }

        // 把 UIScreen 整棵树（含元素 / 绑定 / 动画 / 设置）打包成 JSON 文档。
        // SaveToFile 与 SaveToString 共享这段逻辑，避免编辑器历史栈和磁盘格式漂移。
        Json BuildScreenDocument(const UIScreen& screen) {
            Json document{
                { "version", kUiFileVersion },
                { "screenName", screen.GetName() },
                { "themePath", screen.GetThemePath() },
                { "screenSettings", SerializeScreenSettings(screen) },
                { "canvasReferenceResolution", SerializeVec2(screen.GetReferenceResolution()) },
                { "bindings", Json::array() },
                { "animations", Json::array() },
                { "rootElement", SerializeElement(*screen.GetRootCanvas()) }
            };
            for (const UIPropertyBinding& binding : screen.GetBindings()) {
                document["bindings"].push_back(SerializeBinding(binding));
            }
            for (const UIAnimationClip& clip : screen.GetAnimationClips()) {
                document["animations"].push_back(SerializeAnimationClip(clip));
            }
            return document;
        }

        // 反向构造：把 JSON 文档还原成 UIScreen。
        // 失败时返回 nullptr —— 调用方需要自己决定降级策略（保留旧屏幕还是清空）。
        std::unique_ptr<UIScreen> BuildScreenFromDocument(const Json& document) {
            if (!document.is_object()) {
                return nullptr;
            }
            const int version = document.value("version", -1);
            if (version != kUiFileVersion) {
                return nullptr;
            }
            const glm::vec2 referenceResolution = DeserializeVec2(
                document.value("canvasReferenceResolution", Json::object()),
                glm::vec2(1920.0f, 1080.0f)
            );
            auto screen = std::make_unique<UIScreen>(
                document.value("screenName", std::string("UIScreen")),
                referenceResolution
            );
            screen->SetReferenceResolution(referenceResolution);
            screen->SetThemePath(document.value("themePath", std::string{}));
            DeserializeScreenSettings(document.value("screenSettings", Json::object()), *screen);

            if (!document.contains("rootElement") || !document["rootElement"].is_object()) {
                return nullptr;
            }
            auto rootElement = DeserializeElement(document["rootElement"]);
            if (!rootElement) {
                return nullptr;
            }
            screen->CreateRootCanvas(rootElement->name, referenceResolution);
            UIElement* canvas = screen->GetRootCanvas();
            if (!canvas) {
                return nullptr;
            }
            canvas->name = rootElement->name;
            canvas->id = rootElement->id;
            canvas->visible = rootElement->visible;
            canvas->enabled = rootElement->enabled;
            canvas->interactable = rootElement->interactable;
            canvas->runtimeMutable = rootElement->runtimeMutable;
            canvas->zOrder = rootElement->zOrder;
            canvas->transform = rootElement->transform;
            canvas->style = rootElement->style;
            canvas->events = rootElement->events;
            for (auto& child : rootElement->GetChildren()) {
                canvas->AddChild(std::move(child));
            }
            if (document.contains("bindings") && document["bindings"].is_array()) {
                for (const auto& bindingValue : document["bindings"]) {
                    screen->AddBinding(DeserializeBinding(bindingValue));
                }
            }
            if (document.contains("animations") && document["animations"].is_array()) {
                for (const auto& clipValue : document["animations"]) {
                    screen->AddAnimationClip(DeserializeAnimationClip(clipValue));
                }
            }
            return screen;
        }

    } // namespace

    bool UISerializer::SaveToFile(const UIScreen& screen, const std::filesystem::path& path) {
        if (!screen.GetRootCanvas() || path.empty() || !HasUiJsonExtension(path)) {
            return false;
        }

        std::error_code ec;
        const std::filesystem::path parentPath = path.parent_path();
        if (!parentPath.empty()) {
            std::filesystem::create_directories(parentPath, ec);
            if (ec) {
                return false;
            }
        }

        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        if (!output) {
            return false;
        }

        const Json document = BuildScreenDocument(screen);
        output << document.dump(4);
        return static_cast<bool>(output);
    }

    std::unique_ptr<UIScreen> UISerializer::LoadFromFile(const std::filesystem::path& path) {
        if (path.empty() || !HasUiJsonExtension(path)) {
            return nullptr;
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

        return BuildScreenFromDocument(document);
    }

    std::string UISerializer::SaveToString(const UIScreen& screen) {
        if (!screen.GetRootCanvas()) {
            return {};
        }
        // 不缩进 —— 历史栈拿去做相等比较和占内存，紧凑形式更友好。
        return BuildScreenDocument(screen).dump();
    }

    std::unique_ptr<UIScreen> UISerializer::LoadFromString(const std::string& json) {
        if (json.empty()) {
            return nullptr;
        }
        Json document;
        try {
            document = Json::parse(json);
        }
        catch (...) {
            return nullptr;
        }
        return BuildScreenFromDocument(document);
    }

} // namespace engine
