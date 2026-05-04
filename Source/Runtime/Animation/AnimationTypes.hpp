#pragma once

#include <cstdint>
#include <limits>
#include <string>

#include "../Scene/model_loader/engine_model.hpp"

namespace engine {

constexpr uint32_t kInvalidSkeletonIndex = std::numeric_limits<uint32_t>::max();
constexpr uint32_t kInvalidPoseIndex = std::numeric_limits<uint32_t>::max();
constexpr uint32_t kInvalidClipIndex = std::numeric_limits<uint32_t>::max();

using Skeleton = EngineSkeleton;
using SkeletonPose = EngineSkeletonPose;

struct AnimationClip {
    std::string name;
    uint32_t clipIndex = kInvalidClipIndex;
    uint32_t skeletonIndex = kInvalidSkeletonIndex;
    float durationSeconds = 0.0f;

    bool IsValid() const {
        return clipIndex != kInvalidClipIndex && skeletonIndex != kInvalidSkeletonIndex && durationSeconds >= 0.0f;
    }
};

struct AnimationPlaybackState {
    float currentTimeSeconds = 0.0f;
    float playbackSpeed = 1.0f;
    bool loop = true;
    bool playing = false;
};

struct AnimationBinding {
    uint32_t skeletonIndex = kInvalidSkeletonIndex;
    uint32_t poseIndex = kInvalidPoseIndex;

    bool IsValid() const {
        return skeletonIndex != kInvalidSkeletonIndex && poseIndex != kInvalidPoseIndex;
    }
};

} // namespace engine
