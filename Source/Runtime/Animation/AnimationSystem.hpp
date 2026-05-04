#pragma once

#include <vector>
#include "../Core/System.h"
#include "../Scene/model_loader/engine_model.hpp"
#include "Animator.hpp"
#include "OzzAnimationBridge.hpp"

namespace engine {

class AnimationSystem final : public System {
public:
    AnimationSystem() = default;

    void Init() override;
    void Update(float dt) override;
    void Shutdown() override;

    void SetModel(EngineModel* model) { mModel = model; }
    EngineModel* GetModel() const { return mModel; }

    Animator& CreateAnimator();
    Animator* GetAnimator(size_t index);
    const Animator* GetAnimator(size_t index) const;

    AnimationClip MakeClipReference(size_t clipIndex) const;
    AnimationClip FindClipByName(const std::string& clipName) const;
    bool BindAnimator(size_t animatorIndex, uint32_t skeletonIndex, uint32_t poseIndex);
    bool PlayClip(size_t animatorIndex, const AnimationClip& clip, bool restart = true);

    OzzAnimationBridge& GetBridge() { return mBridge; }
    const OzzAnimationBridge& GetBridge() const { return mBridge; }

private:
    EngineModel* mModel = nullptr;
    std::vector<Animator> mAnimators;
    OzzAnimationBridge mBridge;
};

} // namespace engine
