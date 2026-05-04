#include "OzzAnimationBridge.hpp"

#include <algorithm>
#include <vector>

#include <glm/gtc/quaternion.hpp>

#include "ozz/animation/runtime/animation.h"
#include "ozz/animation/runtime/sampling_job.h"
#include "ozz/animation/runtime/skeleton.h"
#include "ozz/base/maths/simd_math.h"
#include "ozz/base/maths/soa_transform.h"

namespace engine {
namespace {

glm::mat4 MakeTransformFromSoaLane(const ozz::math::SoaTransform& soaTransform, int lane)
{
    float tx[4], ty[4], tz[4];
    float qx[4], qy[4], qz[4], qw[4];
    float sx[4], sy[4], sz[4];

    ozz::math::StorePtrU(soaTransform.translation.x, tx);
    ozz::math::StorePtrU(soaTransform.translation.y, ty);
    ozz::math::StorePtrU(soaTransform.translation.z, tz);
    ozz::math::StorePtrU(soaTransform.rotation.x, qx);
    ozz::math::StorePtrU(soaTransform.rotation.y, qy);
    ozz::math::StorePtrU(soaTransform.rotation.z, qz);
    ozz::math::StorePtrU(soaTransform.rotation.w, qw);
    ozz::math::StorePtrU(soaTransform.scale.x, sx);
    ozz::math::StorePtrU(soaTransform.scale.y, sy);
    ozz::math::StorePtrU(soaTransform.scale.z, sz);

    return glm::translate(glm::mat4(1.0f), glm::vec3(tx[lane], ty[lane], tz[lane])) *
        glm::mat4_cast(glm::normalize(glm::quat(qw[lane], qx[lane], qy[lane], qz[lane]))) *
        glm::scale(glm::mat4(1.0f), glm::vec3(sx[lane], sy[lane], sz[lane]));
}

float ComputeRatio(const ozz::animation::Animation& animation, float timeSeconds)
{
    const float duration = animation.duration();
    if (duration <= 0.0f) {
        return 0.0f;
    }
    return std::clamp(timeSeconds / duration, 0.0f, 1.0f);
}

} // namespace

bool OzzAnimationBridge::IsAvailable() const
{
    return true;
}

bool OzzAnimationBridge::BuildSkeleton(const Skeleton& skeleton, OzzSkeletonHandle& outHandle) const
{
    outHandle.runtimeSkeleton = skeleton.runtimeSkeleton;
    return outHandle.runtimeSkeleton != nullptr;
}

bool OzzAnimationBridge::BuildClip(const AnimationClip& clip, OzzClipHandle& outHandle) const
{
    outHandle.runtimeAnimation = clip.clipIndex == kInvalidClipIndex ? nullptr : nullptr;
    return false;
}

bool OzzAnimationBridge::SampleClip(const OzzSkeletonHandle& skeletonHandle,
    const OzzClipHandle& clipHandle,
    const AnimationPlaybackState& playback,
    SkeletonPose& inOutPose) const
{
    auto* runtimeSkeleton = static_cast<ozz::animation::Skeleton*>(skeletonHandle.runtimeSkeleton);
    auto* runtimeAnimation = static_cast<ozz::animation::Animation*>(clipHandle.runtimeAnimation);
    if (!runtimeSkeleton || !runtimeAnimation) {
        return false;
    }

    std::vector<ozz::math::SoaTransform> localTransforms(runtimeSkeleton->num_soa_joints());
    ozz::animation::SamplingJob::Context context(runtimeAnimation->num_tracks());

    ozz::animation::SamplingJob samplingJob;
    samplingJob.animation = runtimeAnimation;
    samplingJob.context = &context;
    samplingJob.ratio = ComputeRatio(*runtimeAnimation, playback.currentTimeSeconds);
    samplingJob.output = ozz::make_span(localTransforms);
    if (!samplingJob.Run()) {
        return false;
    }

    const size_t jointCount = inOutPose.localJointTransforms.size();
    if (jointCount == 0) {
        return false;
    }

    for (size_t jointIndex = 0; jointIndex < jointCount; ++jointIndex) {
        const size_t soaIndex = jointIndex / 4;
        const int lane = static_cast<int>(jointIndex % 4);
        inOutPose.localJointTransforms[jointIndex] = MakeTransformFromSoaLane(localTransforms[soaIndex], lane);
    }
    return true;
}

} // namespace engine
