#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

#include "UIBinding.hpp"

namespace engine {

    class UIScreen;

    // 动画可驱动的元素属性类型。
    enum class UIAnimationProperty : std::uint8_t {
        Position = 0,       // 位置 (Vec2)
        Size,               // 尺寸 (Vec2)
        Scale,              // 缩放 (Vec2)
        Rotation,           // 旋转角度 (Float)
        Opacity,            // 不透明度 (Float)
        BackgroundColor,    // 背景颜色 (Color)
        TintColor,          // 染色 (Color)
        TextColor           // 文字颜色 (Color)
    };

    // 关键帧之间的缓动曲线类型。
    enum class UIAnimationEasing : std::uint8_t {
        Linear = 0,     // 匀速
        EaseIn,         // 慢入
        EaseOut,        // 慢出
        EaseInOut,      // 慢入慢出
        EaseOutStrong   // 强慢出（更明显的减速效果）
    };

    // 单个关键帧 —— 在指定时间点设置属性值，并指定到下一帧的缓动方式。
    struct UIKeyframe {
        float time = 0.0f;                                  // 时间点（秒）
        UIValue value;                                       // 该时间点的属性值
        UIAnimationEasing easing = UIAnimationEasing::Linear; // 缓动类型
    };

    // 动画轨道 —— 对单个元素的单个属性进行关键帧驱动。
    struct UIAnimationTrack {
        UIElementId targetElementId = 0;                          // 驱动的目标元素 ID
        UIAnimationProperty property = UIAnimationProperty::Opacity; // 驱动的属性
        std::vector<UIKeyframe> keyframes;                         // 关键帧列表（按时间排序）
    };

    // 动画片段 —— 由多条轨道组合而成的一段完整动画。
    struct UIAnimationClip {
        std::string name;           // 片段名称（在 screenSettings 中引用）
        float duration = 0.0f;      // 总时长（秒）
        int loopCount = 1;          // 循环次数（0 = 无限循环）
        bool playOnShow = false;    // 屏幕显示时是否自动播放
        std::vector<UIAnimationTrack> tracks; // 轨道列表
    };

    // 动画播放器 —— 管理一个 UIScreen 上所有正在播放的动画片段。
    // 每帧调用 Update() 推进时间，采样关键帧并写入目标元素属性。
    class UIAnimator {
    public:
        // 单条正在播放的片段的运行时状态。
        struct ActiveClipState {
            std::string clipName;           // 关联的片段名
            float currentTime = 0.0f;       // 当前播放时间
            int completedLoops = 0;         // 已完成的循环次数
            bool paused = false;            // 是否暂停
            std::unordered_set<UIElementId> warnedMissingTargets; // 已警告过的缺失目标（避免重复日志）
        };

        // 设置此播放器关联的屏幕（用于查找片段定义和目标元素）。
        void SetScreen(UIScreen* screen);
        UIScreen* GetScreen() { return mScreen; }
        const UIScreen* GetScreen() const { return mScreen; }

        // 播放 / 停止 / 暂停 / 恢复指定名称的动画片段。
        bool Play(std::string_view clipName);
        void Stop(std::string_view clipName);
        void Pause(std::string_view clipName);
        void Resume(std::string_view clipName);
        bool IsPlaying(std::string_view clipName) const;
        // 停止所有正在播放的片段。
        void StopAll();
        // 每帧调用：推进时间、采样关键帧、应用到元素属性。
        void Update(float deltaTime);

        const std::vector<ActiveClipState>& GetActiveClips() const { return mActiveClips; }

    private:
        // 按名称查找当前屏幕中定义的动画片段。
        const UIAnimationClip* FindClip(std::string_view clipName) const;
        // 在指定时间点采样片段的所有轨道并写入属性。
        void ApplyClipAtTime(const UIAnimationClip& clip, float sampleTime, ActiveClipState& state);
        // 采样单条轨道并写入对应元素属性，成功返回 true。
        bool ApplyTrackSample(const UIAnimationTrack& track, float sampleTime, ActiveClipState& state);
        // 根据缓动类型计算 0-1 范围内的插值因子。
        static float EvaluateEasing(UIAnimationEasing easing, float t);
        // 采样浮点型轨道（Opacity / Rotation）。
        static bool TrySampleNumericTrack(const UIAnimationTrack& track, float sampleTime, float& outValue);
        // 采样 Vec2 型轨道（Position / Scale / Size）。
        static bool TrySampleVec2Track(const UIAnimationTrack& track, float sampleTime, glm::vec2& outValue);
        // 采样颜色型轨道（BackgroundColor / TintColor / TextColor）。
        static bool TrySampleColorTrack(const UIAnimationTrack& track, float sampleTime, glm::vec4& outValue);

    private:
        UIScreen* mScreen = nullptr;                    // 关联的屏幕（不拥有所有权）
        std::vector<ActiveClipState> mActiveClips;      // 当前正在播放的所有片段状态
    };

    // 动画属性枚举 <-> 字符串转换。
    std::string_view ToString(UIAnimationProperty property);
    std::string_view ToString(UIAnimationEasing easing);
    // 字符串 -> 动画属性 / 缓动枚举解析。
    bool TryParseUIAnimationProperty(std::string_view name, UIAnimationProperty& outProperty);
    bool TryParseUIAnimationEasing(std::string_view name, UIAnimationEasing& outEasing);

} // namespace engine
