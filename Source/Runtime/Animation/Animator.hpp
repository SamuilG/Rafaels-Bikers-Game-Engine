#pragma once

#include <string>

#include "AnimationTypes.hpp"

namespace engine {

class Animator {
public:
    Animator() = default;

    void Bind(uint32_t skeletonIndex, uint32_t poseIndex);
    void SetClip(const AnimationClip& clip);

    void Play(bool restart = false);
    void Pause();
    void Stop();
    void Update(float dt);

    const AnimationBinding& GetBinding() const { return mBinding; }
    const AnimationClip& GetClip() const { return mClip; }
    const AnimationPlaybackState& GetPlaybackState() const { return mPlayback; }

    bool IsBound() const { return mBinding.IsValid(); }
    bool HasClip() const { return mClip.IsValid(); }
    bool IsPlaying() const { return mPlayback.playing; }

private:
    AnimationBinding mBinding{};
    AnimationClip mClip{};
    AnimationPlaybackState mPlayback{};
};

} // namespace engine
