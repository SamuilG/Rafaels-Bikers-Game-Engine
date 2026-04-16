#include "AnimationSystem.hpp"
#include "../Scene/SceneManager.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/matrix_decompose.hpp>
#include <cstring>
#include <print>

namespace engine {

// ---------------------------------------------------------------------------
// register_model: copy nodes/skins/animations from a loaded EngineModel
// ---------------------------------------------------------------------------
AnimationSystem::Registration AnimationSystem::register_model(const EngineModel& model)
{
    Registration reg{};
    reg.baseNodeIndex = static_cast<uint32_t>(mNodes.size());
    reg.baseSkinIndex = static_cast<uint32_t>(mSkins.size());
    reg.baseAnimIndex = static_cast<uint32_t>(mAnims.size());

    // Append nodes (remap parent indices by adding reg.baseNodeIndex)
    for (auto& n : model.nodes) {
        EngineNode copy = n;
        if (copy.parentIndex >= 0)
            copy.parentIndex += (int)reg.baseNodeIndex;
        mNodes.push_back(std::move(copy));
    }

    // Append skins (remap joint indices by adding reg.baseNodeIndex)
    for (auto& s : model.skins) {
        EngineSkin copy = s;
        for (int& j : copy.joints)
            if (j >= 0) j += (int)reg.baseNodeIndex;
        if (copy.skeletonRoot >= 0)
            copy.skeletonRoot += (int)reg.baseNodeIndex;
        mSkins.push_back(std::move(copy));
        mNodeBasePerSkin.push_back(reg.baseNodeIndex);
    }

    // Append animations (remap channel node indices by adding reg.baseNodeIndex)
    for (auto& a : model.animations) {
        EngineAnimation copy = a;
        for (auto& ch : copy.channels)
            if (ch.nodeIndex >= 0) ch.nodeIndex += (int)reg.baseNodeIndex;
        mAnims.push_back(std::move(copy));
    }

    std::print("[AnimationSystem] Registered model: {} nodes, {} skins, {} animations\n",
               model.nodes.size(), model.skins.size(), model.animations.size());
    return reg;
}

// ---------------------------------------------------------------------------
// sample_sampler: linear or step interpolation at time t
// ---------------------------------------------------------------------------
glm::vec4 AnimationSystem::sample_sampler(const EngineAnimSampler& s, float t, bool isRotation) const
{
    if (s.times.empty() || s.values.empty())
        return glm::vec4(0.0f, 0.0f, 0.0f, isRotation ? 1.0f : 0.0f);

    if (t <= s.times.front()) return s.values.front();
    if (t >= s.times.back())  return s.values.back();

    // Binary search for the surrounding interval
    size_t lo = 0, hi = s.times.size() - 1;
    while (hi - lo > 1) {
        size_t mid = (lo + hi) / 2;
        if (s.times[mid] <= t) lo = mid; else hi = mid;
    }

    if (s.interp == 1) // STEP
        return s.values[lo];

    // LINEAR
    float dt    = s.times[hi] - s.times[lo];
    float alpha = (dt > 1e-6f) ? (t - s.times[lo]) / dt : 0.0f;

    if (isRotation) {
        // Spherical linear interpolation for quaternions
        glm::quat q0 = glm::quat(s.values[lo].w, s.values[lo].x, s.values[lo].y, s.values[lo].z);
        glm::quat q1 = glm::quat(s.values[hi].w, s.values[hi].x, s.values[hi].y, s.values[hi].z);
        glm::quat r  = glm::normalize(glm::slerp(q0, q1, alpha));
        return glm::vec4(r.x, r.y, r.z, r.w);
    }
    else {
        glm::vec4 v = glm::mix(s.values[lo], s.values[hi], alpha);
        return v;
    }
}

// ---------------------------------------------------------------------------
// compute_global: walk up the node tree to build the global matrix
// ---------------------------------------------------------------------------
glm::mat4 AnimationSystem::compute_global(int nodeIdx, uint32_t /*nodeBase*/,
                                           const std::vector<NodeState>& states) const
{
    if (nodeIdx < 0 || nodeIdx >= (int)mNodes.size())
        return glm::mat4(1.0f);

    // Build local matrix from animated state (or fall back to default)
    glm::mat4 local;
    if (nodeIdx < (int)states.size() && states[nodeIdx].hasTRS) {
        const NodeState& ns = states[nodeIdx];
        local = glm::translate(glm::mat4(1.0f), ns.translation)
              * glm::mat4_cast(ns.rotation)
              * glm::scale(glm::mat4(1.0f), ns.scale);
    } else {
        local = mNodes[nodeIdx].localTransform;
    }

    int parent = mNodes[nodeIdx].parentIndex;
    if (parent < 0) return local;
    return compute_global(parent, 0, states) * local;
}

// ---------------------------------------------------------------------------
// evaluate_skin: compute bone matrices for a skin driven by an animation
// ---------------------------------------------------------------------------
void AnimationSystem::evaluate_skin(int skinIdx, int animIdx, float t,
                                    std::vector<glm::mat4>& outMatrices)
{
    if (skinIdx < 0 || skinIdx >= (int)mSkins.size()) return;
    const EngineSkin& skin = mSkins[skinIdx];
    outMatrices.resize(skin.joints.size(), glm::mat4(1.0f));

    // Build per-node animated TRS states for this frame
    std::vector<NodeState> states(mNodes.size());

    if (animIdx >= 0 && animIdx < (int)mAnims.size()) {
        const EngineAnimation& anim = mAnims[animIdx];
        for (const auto& ch : anim.channels) {
            if (ch.nodeIndex < 0 || ch.nodeIndex >= (int)mNodes.size()) continue;
            if (ch.samplerIndex < 0 || ch.samplerIndex >= (int)anim.samplers.size()) continue;

            NodeState& ns = states[ch.nodeIndex];
            if (!ns.hasTRS) {
                // Initialize from default node transform
                glm::vec3 skew; glm::vec4 persp; glm::vec3 def_s, def_t; glm::quat def_r;
                glm::decompose(mNodes[ch.nodeIndex].localTransform, def_s, def_r, def_t, skew, persp);
                ns.translation = def_t;
                ns.rotation    = def_r;
                ns.scale       = def_s;
                ns.hasTRS      = true;
            }

            const EngineAnimSampler& samp = anim.samplers[ch.samplerIndex];
            if (ch.path == 0) { // translation
                glm::vec4 v = sample_sampler(samp, t, false);
                ns.translation = glm::vec3(v);
            } else if (ch.path == 1) { // rotation
                glm::vec4 v = sample_sampler(samp, t, true);
                ns.rotation = glm::quat(v.w, v.x, v.y, v.z);
            } else if (ch.path == 2) { // scale
                glm::vec4 v = sample_sampler(samp, t, false);
                ns.scale = glm::vec3(v);
            }
        }
    }

    // Compute final bone matrices = globalMatrix(joint) * inverseBindMatrix
    for (size_t i = 0; i < skin.joints.size(); ++i) {
        glm::mat4 global = compute_global(skin.joints[i], 0, states);
        const glm::mat4& ibm = (i < skin.inverseBindMatrices.size())
                               ? skin.inverseBindMatrices[i]
                               : glm::mat4(1.0f);
        outMatrices[i] = global * ibm;
    }
}

// ---------------------------------------------------------------------------
// Update: advance all animated entities and recompute their bone matrices
// ---------------------------------------------------------------------------
void AnimationSystem::Update(float dt)
{
    if (!mSceneManager) return;

    mSceneManager->get_world()
        .query<AnimationComponent, SkinComponent>()
        .each([&](flecs::entity /*e*/, AnimationComponent& ac, SkinComponent& sc)
    {
        if (!ac.playing || ac.animIndex < 0 || ac.animIndex >= (int)mAnims.size()) return;

        const EngineAnimation& anim = mAnims[ac.animIndex];

        // Advance time
        if (anim.duration > 0.0f) {
            ac.currentTime += dt * ac.speed;
            if (ac.looping) {
                while (ac.currentTime > anim.duration)
                    ac.currentTime -= anim.duration;
            } else {
                if (ac.currentTime > anim.duration)
                    ac.currentTime = anim.duration;
            }
        }

        // Compute bone matrices for this entity's skin
        evaluate_skin(sc.skinIndex, ac.animIndex, ac.currentTime, sc.boneMatrices);
    });
}

} // namespace engine
