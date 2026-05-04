#pragma once

#include "AnimationTypes.hpp"

namespace engine {

struct OzzSkeletonHandle {
    void* runtimeSkeleton = nullptr;
};

struct OzzClipHandle {
    void* runtimeAnimation = nullptr;
};

class OzzAnimationBridge {
public:
    OzzAnimationBridge() = default;

    bool IsAvailable() const;

    bool BuildSkeleton(const Skeleton& skeleton, OzzSkeletonHandle& outHandle) const;
    bool BuildClip(const AnimationClip& clip, OzzClipHandle& outHandle) const;
    bool SampleClip(const OzzSkeletonHandle& skeletonHandle,
        const OzzClipHandle& clipHandle,
        const AnimationPlaybackState& playback,
        SkeletonPose& inOutPose) const;
};

} // namespace engine
