#include "UIAnimation.hpp"

#include <algorithm>
#include <cmath>
#include <format>

#include "../EngineUi.hpp"
#include "UIScreen.hpp"

namespace engine {

    // 匿名命名空间，包含动画采样的内部辅助函数
    namespace {

        // 尝试从关键帧中读取指定类型的值，成功返回true
        template <typename TValue>
        bool TryReadKeyframeValue(const UIKeyframe& keyframe, TValue& outValue) {
            if (const auto* value = std::get_if<TValue>(&keyframe.value.data)) {
                outValue = *value;
                return true;
            }
            return false;
        }

        // 对动画轨道进行采样，根据时间在关键帧之间插值计算当前值
        template <typename TValue, typename TLerp>
        bool TrySampleTrackValue(
            const UIAnimationTrack& track,
            float sampleTime,
            TValue& outValue,
            TLerp&& lerpFn,
            float(*easingFn)(UIAnimationEasing, float))
        {
            // 无关键帧则无法采样
            if (track.keyframes.empty()) {
                return false;
            }

            // 只有一个关键帧或采样时间在第一帧之前，直接返回首帧值
            if (track.keyframes.size() == 1 || sampleTime <= track.keyframes.front().time) {
                return TryReadKeyframeValue(track.keyframes.front(), outValue);
            }

            // 采样时间超过最后一帧，直接返回末帧值
            if (sampleTime >= track.keyframes.back().time) {
                return TryReadKeyframeValue(track.keyframes.back(), outValue);
            }

            // 遍历相邻关键帧对，找到采样时间所在的区间并进行插值
            for (std::size_t index = 0; index + 1 < track.keyframes.size(); ++index) {
                const UIKeyframe& from = track.keyframes[index];
                const UIKeyframe& to = track.keyframes[index + 1];
                if (sampleTime < from.time || sampleTime > to.time) {
                    continue;
                }

                TValue fromValue{};
                TValue toValue{};
                if (!TryReadKeyframeValue(from, fromValue) || !TryReadKeyframeValue(to, toValue)) {
                    return false;
                }

                // 计算归一化时间，应用缓动函数后进行线性插值
                const float segmentDuration = std::max(0.0001f, to.time - from.time);
                const float normalized = std::clamp((sampleTime - from.time) / segmentDuration, 0.0f, 1.0f);
                const float eased = easingFn(from.easing, normalized);
                outValue = lerpFn(fromValue, toValue, eased);
                return true;
            }

            return TryReadKeyframeValue(track.keyframes.back(), outValue);
        }

    } // namespace

    // 将动画属性枚举转换为对应的字符串名称
    std::string_view ToString(UIAnimationProperty property) {
        switch (property) {
        case UIAnimationProperty::Position: return "Position";
        case UIAnimationProperty::Size: return "Size";
        case UIAnimationProperty::Scale: return "Scale";
        case UIAnimationProperty::Rotation: return "Rotation";
        case UIAnimationProperty::Opacity: return "Opacity";
        case UIAnimationProperty::BackgroundColor: return "BackgroundColor";
        case UIAnimationProperty::TintColor: return "TintColor";
        case UIAnimationProperty::TextColor: return "TextColor";
        default: return "Opacity";
        }
    }

    // 将缓动类型枚举转换为对应的字符串名称
    std::string_view ToString(UIAnimationEasing easing) {
        switch (easing) {
        case UIAnimationEasing::Linear: return "Linear";
        case UIAnimationEasing::EaseIn: return "EaseIn";
        case UIAnimationEasing::EaseOut: return "EaseOut";
        case UIAnimationEasing::EaseInOut: return "EaseInOut";
        case UIAnimationEasing::EaseOutStrong: return "EaseOutStrong";
        default: return "Linear";
        }
    }

    // 根据字符串名称解析动画属性枚举，成功返回true
    bool TryParseUIAnimationProperty(std::string_view name, UIAnimationProperty& outProperty) {
        for (UIAnimationProperty property : {
            UIAnimationProperty::Position,
            UIAnimationProperty::Size,
            UIAnimationProperty::Scale,
            UIAnimationProperty::Rotation,
            UIAnimationProperty::Opacity,
            UIAnimationProperty::BackgroundColor,
            UIAnimationProperty::TintColor,
            UIAnimationProperty::TextColor }) {
            if (ToString(property) == name) {
                outProperty = property;
                return true;
            }
        }
        return false;
    }

    // 根据字符串名称解析缓动类型枚举，成功返回true
    bool TryParseUIAnimationEasing(std::string_view name, UIAnimationEasing& outEasing) {
        for (UIAnimationEasing easing : {
            UIAnimationEasing::Linear,
            UIAnimationEasing::EaseIn,
            UIAnimationEasing::EaseOut,
            UIAnimationEasing::EaseInOut,
            UIAnimationEasing::EaseOutStrong }) {
            if (ToString(easing) == name) {
                outEasing = easing;
                return true;
            }
        }
        return false;
    }

    // 设置动画器关联的UI屏幕，并清除所有活跃的动画片段
    void UIAnimator::SetScreen(UIScreen* screen) {
        mScreen = screen;
        mActiveClips.clear();
    }

    // 播放指定名称的动画片段；若已在播放则重置到起始状态
    bool UIAnimator::Play(std::string_view clipName) {
        const UIAnimationClip* clip = FindClip(clipName);
        if (!clip) {
            EngineUi::LogPrint("[UIAnimator][Warning] Missing clip '{}'\n", clipName);
            return false;
        }

        // 查找是否已存在同名的活跃片段
        auto existing = std::find_if(mActiveClips.begin(), mActiveClips.end(), [&](const ActiveClipState& state) {
            return state.clipName == clipName;
        });
        if (existing == mActiveClips.end()) {
            mActiveClips.push_back(ActiveClipState{ std::string(clipName) });
            existing = std::prev(mActiveClips.end());
        }
        else {
            existing->currentTime = 0.0f;
            existing->completedLoops = 0;
            existing->warnedMissingTargets.clear();
        }

        existing->paused = false;
        ApplyClipAtTime(*clip, 0.0f, *existing);
        return true;
    }

    // 停止并移除指定名称的动画片段
    void UIAnimator::Stop(std::string_view clipName) {
        std::erase_if(mActiveClips, [&](const ActiveClipState& state) {
            return state.clipName == clipName;
        });
    }

    // 暂停指定名称的动画片段
    void UIAnimator::Pause(std::string_view clipName) {
        for (ActiveClipState& state : mActiveClips) {
            if (state.clipName == clipName) {
                state.paused = true;
            }
        }
    }

    // 恢复播放指定名称的已暂停动画片段
    void UIAnimator::Resume(std::string_view clipName) {
        for (ActiveClipState& state : mActiveClips) {
            if (state.clipName == clipName) {
                state.paused = false;
            }
        }
    }

    // 检查指定名称的动画片段是否正在播放
    bool UIAnimator::IsPlaying(std::string_view clipName) const {
        return std::any_of(mActiveClips.begin(), mActiveClips.end(), [&](const ActiveClipState& state) {
            return state.clipName == clipName;
        });
    }

    // 停止所有正在播放的动画片段
    void UIAnimator::StopAll() {
        mActiveClips.clear();
    }

    // 每帧更新所有活跃的动画片段，处理时间推进、循环和结束逻辑
    void UIAnimator::Update(float deltaTime) {
        if (!mScreen || mActiveClips.empty()) {
            return;
        }

        // 收集需要停止的片段，避免在遍历时修改容器
        std::vector<std::string> clipsToStop;
        for (ActiveClipState& state : mActiveClips) {
            const UIAnimationClip* clip = FindClip(state.clipName);
            if (!clip) {
                clipsToStop.push_back(state.clipName);
                continue;
            }
            if (state.paused) {
                ApplyClipAtTime(*clip, state.currentTime, state);
                continue;
            }

            // 持续时间为零的片段立即应用并标记停止
            if (clip->duration <= 0.0f) {
                ApplyClipAtTime(*clip, 0.0f, state);
                clipsToStop.push_back(state.clipName);
                continue;
            }

            // 推进时间并处理循环：超过片段时长时回绕或结束
            state.currentTime += std::max(0.0f, deltaTime);

            bool finished = false;
            while (state.currentTime >= clip->duration) {
                if (clip->loopCount > 0 && state.completedLoops + 1 >= clip->loopCount) {
                    state.currentTime = clip->duration;
                    finished = true;
                    break;
                }

                state.currentTime -= clip->duration;
                ++state.completedLoops;
            }

            ApplyClipAtTime(*clip, state.currentTime, state);
            if (finished) {
                clipsToStop.push_back(state.clipName);
            }
        }

        // 遍历结束后统一停止已完成的片段
        for (const std::string& clipName : clipsToStop) {
            Stop(clipName);
        }
    }

    // 在当前屏幕的动画片段列表中查找指定名称的片段
    const UIAnimationClip* UIAnimator::FindClip(std::string_view clipName) const {
        if (!mScreen) {
            return nullptr;
        }

        const auto& clips = mScreen->GetAnimationClips();
        const auto iterator = std::find_if(clips.begin(), clips.end(), [&](const UIAnimationClip& clip) {
            return clip.name == clipName;
        });
        return iterator == clips.end() ? nullptr : &(*iterator);
    }

    // 在指定时间点应用动画片段的所有轨道到对应的UI元素
    void UIAnimator::ApplyClipAtTime(const UIAnimationClip& clip, float sampleTime, ActiveClipState& state) {
        if (!mScreen) {
            return;
        }

        const float clampedTime = std::clamp(sampleTime, 0.0f, std::max(0.0f, clip.duration));
        for (const UIAnimationTrack& track : clip.tracks) {
            ApplyTrackSample(track, clampedTime, state);
        }
    }

    // 对单条动画轨道进行采样，并将结果应用到目标UI元素的对应属性上
    bool UIAnimator::ApplyTrackSample(const UIAnimationTrack& track, float sampleTime, ActiveClipState& state) {
        if (!mScreen || track.targetElementId == 0) {
            return false;
        }

        UIElement* target = mScreen->FindById(track.targetElementId);
        if (!target) {
            if (!state.warnedMissingTargets.contains(track.targetElementId)) {
                state.warnedMissingTargets.insert(track.targetElementId);
                EngineUi::LogPrint(
                    "[UIAnimator][Warning] Missing target element {} for clip '{}' on screen '{}'\n",
                    track.targetElementId,
                    state.clipName,
                    mScreen->GetName());
            }
            return false;
        }

        switch (track.property) {
        case UIAnimationProperty::Position: {
            glm::vec2 value(0.0f);
            if (TrySampleVec2Track(track, sampleTime, value)) {
                target->transform.position = value;
                return true;
            }
            break;
        }
        case UIAnimationProperty::Size: {
            glm::vec2 value(0.0f);
            if (TrySampleVec2Track(track, sampleTime, value)) {
                target->transform.size = value;
                return true;
            }
            break;
        }
        case UIAnimationProperty::Scale: {
            glm::vec2 value(1.0f);
            if (TrySampleVec2Track(track, sampleTime, value)) {
                target->transform.scale = value;
                return true;
            }
            break;
        }
        case UIAnimationProperty::Rotation: {
            float value = 0.0f;
            if (TrySampleNumericTrack(track, sampleTime, value)) {
                target->transform.rotation = value;
                return true;
            }
            break;
        }
        case UIAnimationProperty::Opacity: {
            float value = 1.0f;
            if (TrySampleNumericTrack(track, sampleTime, value)) {
                target->style.opacity = std::clamp(value, 0.0f, 1.0f);
                return true;
            }
            break;
        }
        case UIAnimationProperty::BackgroundColor: {
            glm::vec4 value(1.0f);
            if (TrySampleColorTrack(track, sampleTime, value)) {
                target->style.backgroundColor = value;
                return true;
            }
            break;
        }
        case UIAnimationProperty::TintColor: {
            glm::vec4 value(1.0f);
            if (TrySampleColorTrack(track, sampleTime, value)) {
                target->style.tintColor = value;
                return true;
            }
            break;
        }
        case UIAnimationProperty::TextColor: {
            glm::vec4 value(1.0f);
            if (TrySampleColorTrack(track, sampleTime, value)) {
                target->style.textColor = value;
                return true;
            }
            break;
        }
        }

        return false;
    }

    float UIAnimator::EvaluateEasing(UIAnimationEasing easing, float t) {
        t = std::clamp(t, 0.0f, 1.0f);
        switch (easing) {
        case UIAnimationEasing::EaseIn:
            return t * t;
        case UIAnimationEasing::EaseOut:
            return 1.0f - (1.0f - t) * (1.0f - t);
        case UIAnimationEasing::EaseInOut:
            if (t < 0.5f) {
                return 2.0f * t * t;
            }
            return 1.0f - std::pow(-2.0f * t + 2.0f, 2.0f) * 0.5f;
        case UIAnimationEasing::EaseOutStrong:
            return 1.0f - std::pow(1.0f - t, 4.0f);
        case UIAnimationEasing::Linear:
        default:
            return t;
        }
    }

    bool UIAnimator::TrySampleNumericTrack(const UIAnimationTrack& track, float sampleTime, float& outValue) {
        return TrySampleTrackValue<float>(
            track,
            sampleTime,
            outValue,
            [](float from, float to, float t) {
                return from + (to - from) * t;
            },
            &UIAnimator::EvaluateEasing);
    }

    bool UIAnimator::TrySampleVec2Track(const UIAnimationTrack& track, float sampleTime, glm::vec2& outValue) {
        return TrySampleTrackValue<glm::vec2>(
            track,
            sampleTime,
            outValue,
            [](const glm::vec2& from, const glm::vec2& to, float t) {
                return glm::mix(from, to, t);
            },
            &UIAnimator::EvaluateEasing);
    }

    bool UIAnimator::TrySampleColorTrack(const UIAnimationTrack& track, float sampleTime, glm::vec4& outValue) {
        return TrySampleTrackValue<glm::vec4>(
            track,
            sampleTime,
            outValue,
            [](const glm::vec4& from, const glm::vec4& to, float t) {
                return glm::mix(from, to, t);
            },
            &UIAnimator::EvaluateEasing);
    }

} // namespace engine
