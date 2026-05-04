#include "Animator.hpp"

#include <algorithm>

namespace engine {

void Animator::Bind(uint32_t skeletonIndex, uint32_t poseIndex)
{
    mBinding.skeletonIndex = skeletonIndex;
    mBinding.poseIndex = poseIndex;
}

void Animator::SetClip(const AnimationClip& clip)
{
    mClip = clip;
    mPlayback.currentTimeSeconds = 0.0f;
}

void Animator::Play(bool restart)
{
    if (restart) {
        mPlayback.currentTimeSeconds = 0.0f;
    }
    mPlayback.playing = true;
}

void Animator::Pause()
{
    mPlayback.playing = false;
}

void Animator::Stop()
{
    mPlayback.playing = false;
    mPlayback.currentTimeSeconds = 0.0f;
}

void Animator::Update(float dt)
{
    if (!mPlayback.playing || !mClip.IsValid() || dt <= 0.0f) {
        return;
    }

    mPlayback.currentTimeSeconds += dt * mPlayback.playbackSpeed;

    if (mClip.durationSeconds <= 0.0f) {
        mPlayback.currentTimeSeconds = 0.0f;
        return;
    }

    if (mPlayback.loop) {
        while (mPlayback.currentTimeSeconds >= mClip.durationSeconds) {
            mPlayback.currentTimeSeconds -= mClip.durationSeconds;
        }
        while (mPlayback.currentTimeSeconds < 0.0f) {
            mPlayback.currentTimeSeconds += mClip.durationSeconds;
        }
    }
    else {
        mPlayback.currentTimeSeconds = std::clamp(mPlayback.currentTimeSeconds, 0.0f, mClip.durationSeconds);
        if (mPlayback.currentTimeSeconds >= mClip.durationSeconds) {
            mPlayback.playing = false;
        }
    }
}

} // namespace engine
