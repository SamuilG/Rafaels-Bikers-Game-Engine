#include "AnimationSystem.hpp"

#include <string>

#include "../Scene/model_loader/engine_model.hpp"

namespace engine {

void AnimationSystem::Init()
{
}

void AnimationSystem::Update(float dt)
{
    if (!mModel) {
        for (Animator& animator : mAnimators) {
            animator.Update(dt);
        }
        return;
    }

    for (Animator& animator : mAnimators) {
        animator.Update(dt);

        if (!animator.IsBound() || !animator.HasClip()) {
            continue;
        }

        const AnimationBinding& binding = animator.GetBinding();
        const AnimationClip& clipRef = animator.GetClip();
        if (binding.skeletonIndex >= mModel->skeletons.size() ||
            binding.poseIndex >= mModel->skeletonPoses.size() ||
            clipRef.clipIndex >= mModel->animationClips.size()) {
            continue;
        }

        const EngineAnimationClip& modelClip = mModel->animationClips[clipRef.clipIndex];
        if (modelClip.skeletonIndex != binding.skeletonIndex) {
            continue;
        }

        EngineSkeletonPose& pose = mModel->skeletonPoses[binding.poseIndex];
        bool sampledWithOzz = false;
        if (modelClip.runtimeAnimation != nullptr && mModel->skeletons[binding.skeletonIndex].runtimeSkeleton != nullptr) {
            OzzSkeletonHandle skeletonHandle{};
            skeletonHandle.runtimeSkeleton = mModel->skeletons[binding.skeletonIndex].runtimeSkeleton;
            OzzClipHandle clipHandle{};
            clipHandle.runtimeAnimation = modelClip.runtimeAnimation;
            sampledWithOzz = mBridge.SampleClip(skeletonHandle, clipHandle, animator.GetPlaybackState(), pose);
            if (sampledWithOzz) {
                update_skeleton_pose_matrices(mModel->skeletons[binding.skeletonIndex], pose);
            }
        }

        if (!sampledWithOzz) {
            sample_animation_clip(
                mModel->skeletons[binding.skeletonIndex],
                modelClip,
                animator.GetPlaybackState().currentTimeSeconds,
                pose);
        }
    }

    update_model_skeleton_poses(*mModel);
}

void AnimationSystem::Shutdown()
{
    mAnimators.clear();
    mModel = nullptr;
}

Animator& AnimationSystem::CreateAnimator()
{
    mAnimators.emplace_back();
    return mAnimators.back();
}

Animator* AnimationSystem::GetAnimator(size_t index)
{
    if (index >= mAnimators.size()) {
        return nullptr;
    }
    return &mAnimators[index];
}

const Animator* AnimationSystem::GetAnimator(size_t index) const
{
    if (index >= mAnimators.size()) {
        return nullptr;
    }
    return &mAnimators[index];
}

AnimationClip AnimationSystem::MakeClipReference(size_t clipIndex) const
{
    AnimationClip clipRef{};
    if (!mModel || clipIndex >= mModel->animationClips.size()) {
        return clipRef;
    }

    const EngineAnimationClip& clip = mModel->animationClips[clipIndex];
    clipRef.name = clip.name;
    clipRef.clipIndex = static_cast<uint32_t>(clipIndex);
    clipRef.skeletonIndex = clip.skeletonIndex;
    clipRef.durationSeconds = clip.durationSeconds;
    return clipRef;
}

AnimationClip AnimationSystem::FindClipByName(const std::string& clipName) const
{
    if (!mModel) {
        return {};
    }

    for (size_t clipIndex = 0; clipIndex < mModel->animationClips.size(); ++clipIndex) {
        if (mModel->animationClips[clipIndex].name == clipName) {
            return MakeClipReference(clipIndex);
        }
    }

    return {};
}

bool AnimationSystem::BindAnimator(size_t animatorIndex, uint32_t skeletonIndex, uint32_t poseIndex)
{
    Animator* animator = GetAnimator(animatorIndex);
    if (!animator || !mModel) {
        return false;
    }
    if (skeletonIndex >= mModel->skeletons.size() || poseIndex >= mModel->skeletonPoses.size()) {
        return false;
    }

    animator->Bind(skeletonIndex, poseIndex);
    return true;
}

bool AnimationSystem::PlayClip(size_t animatorIndex, const AnimationClip& clip, bool restart)
{
    Animator* animator = GetAnimator(animatorIndex);
    if (!animator || !clip.IsValid()) {
        return false;
    }

    const AnimationBinding& binding = animator->GetBinding();
    if (!binding.IsValid() || binding.skeletonIndex != clip.skeletonIndex) {
        return false;
    }

    animator->SetClip(clip);
    animator->Play(restart);
    return true;
}

} // namespace engine
