#include "AnimationSystem.hpp"
#include "../Scene/SceneManager.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/constants.hpp>
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
// evaluate_skin: compute bone matrices for a skin driven by an animation.
// Optionally applies rider IK chains after the animation pose is built.
// ---------------------------------------------------------------------------
void AnimationSystem::evaluate_skin(int skinIdx, int animIdx, float t,
                                    std::vector<glm::mat4>& outMatrices,
                                    const RiderIKComponent* rik,
                                    const glm::mat4& charWorld)
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

    // --- Forward lean: rotate spine before IK so arms reach naturally ---
    if (rik && rik->leanAngleDeg != 0.0f)
        apply_spine_lean(rik->leanAngleDeg, states);

    // --- IK post-processing ---
    if (rik && !rik->chains.empty()) {
        glm::mat4 worldToModel = glm::inverse(charWorld);
        for (const auto& chain : rik->chains) {
            if (chain.enabled)
                apply_two_bone_ik(chain, worldToModel, states);
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
// find_node_by_name
// ---------------------------------------------------------------------------
int AnimationSystem::find_node_by_name(const std::string& name) const
{
    for (int i = 0; i < (int)mNodes.size(); ++i)
        if (mNodes[i].name == name) return i;
    return -1;
}

// ---------------------------------------------------------------------------
// debug_print_nodes – call after loading to discover bone names
// ---------------------------------------------------------------------------
void AnimationSystem::debug_print_nodes() const
{
    std::print("[AnimationSystem] Registered nodes ({}):\n", mNodes.size());
    for (int i = 0; i < (int)mNodes.size(); ++i)
        std::print("  [{:3d}] '{}' (parent={})\n", i, mNodes[i].name, mNodes[i].parentIndex);
}

// ---------------------------------------------------------------------------
// rotate_bone_from_to – rotate nodeIdx's local state so that the direction
// from the node to its child changes from oldDir to newDir (model space).
// ---------------------------------------------------------------------------
void AnimationSystem::rotate_bone_from_to(int nodeIdx,
                                           const glm::vec3& oldDir,
                                           const glm::vec3& newDir,
                                           std::vector<NodeState>& states)
{
    if (nodeIdx < 0 || nodeIdx >= (int)mNodes.size()) return;

    glm::vec3 od = glm::normalize(oldDir);
    glm::vec3 nd = glm::normalize(newDir);
    float dotVal = glm::dot(od, nd);
    if (dotVal > 0.9999f) return; // already aligned

    // Build delta rotation in model space
    glm::quat modelDelta;
    if (dotVal < -0.9999f) {
        // 180° rotation – pick an arbitrary perpendicular axis
        glm::vec3 perp = glm::abs(od.x) < 0.9f ? glm::vec3(1,0,0) : glm::vec3(0,1,0);
        modelDelta = glm::angleAxis(glm::pi<float>(), glm::normalize(glm::cross(od, perp)));
    } else {
        glm::vec3 axis = glm::normalize(glm::cross(od, nd));
        float angle    = glm::acos(glm::clamp(dotVal, -1.0f, 1.0f));
        modelDelta     = glm::angleAxis(angle, axis);
    }

    // Get parent's model-space rotation (to convert delta into local space)
    int parentIdx = mNodes[nodeIdx].parentIndex;
    glm::mat4 parentMat = (parentIdx >= 0)
                        ? compute_global(parentIdx, 0, states)
                        : glm::mat4(1.0f);
    // Normalise columns to extract pure rotation (strip scale)
    glm::mat3 pr3(
        glm::normalize(glm::vec3(parentMat[0])),
        glm::normalize(glm::vec3(parentMat[1])),
        glm::normalize(glm::vec3(parentMat[2]))
    );
    glm::quat parentRot    = glm::normalize(glm::quat_cast(pr3));
    glm::quat invParentRot = glm::inverse(parentRot);

    // Initialise node state from default transform if not yet done
    NodeState& ns = states[nodeIdx];
    if (!ns.hasTRS) {
        glm::vec3 skew; glm::vec4 persp;
        glm::decompose(mNodes[nodeIdx].localTransform, ns.scale, ns.rotation, ns.translation, skew, persp);
        ns.rotation = glm::normalize(ns.rotation);
        ns.hasTRS   = true;
    }

    // new_local_rot = inv(parentModel) * modelDelta * parentModel * local_rot
    ns.rotation = glm::normalize(invParentRot * modelDelta * parentRot * ns.rotation);
}

// ---------------------------------------------------------------------------
// apply_two_bone_ik – analytic 2-bone IK.
// Solves root→mid→end so that end reaches chain.worldTarget.
// worldToModel = inverse of the character entity's world transform.
// ---------------------------------------------------------------------------
void AnimationSystem::apply_two_bone_ik(const IKChainConfig& chain,
                                         const glm::mat4& worldToModel,
                                         std::vector<NodeState>& states)
{
    int rootNode = find_node_by_name(chain.rootBone);
    int midNode  = find_node_by_name(chain.midBone);
    int endNode  = find_node_by_name(chain.endBone);
    if (rootNode < 0 || midNode < 0 || endNode < 0) return;

    // Current model-space joint positions
    glm::vec3 rootPos = glm::vec3(compute_global(rootNode, 0, states)[3]);
    glm::vec3 midPos  = glm::vec3(compute_global(midNode,  0, states)[3]);
    glm::vec3 endPos  = glm::vec3(compute_global(endNode,  0, states)[3]);

    // Convert world target + pole to model space
    glm::vec3 targetM = glm::vec3(worldToModel * glm::vec4(chain.worldTarget, 1.0f));
    glm::vec3 poleM   = glm::vec3(worldToModel * glm::vec4(chain.worldPole,   1.0f));

    float a = glm::length(midPos - rootPos); // upper bone length
    float b = glm::length(endPos - midPos);  // lower bone length
    if (a < 1e-5f || b < 1e-5f) return;

    float dist = glm::length(targetM - rootPos);

    // --- Stretch IK: if target is out of reach, proportionally scale bone translations
    // so the end effector can always reach the target (no clipping to max extension).
    {
        float totalLen = a + b;
        if (dist > totalLen + 1e-4f) {
            float sf = dist / totalLen;
            auto ensureNS = [&](int idx) -> NodeState& {
                NodeState& ns = states[idx];
                if (!ns.hasTRS) {
                    glm::vec3 sk; glm::vec4 pr;
                    glm::decompose(mNodes[idx].localTransform,
                                   ns.scale, ns.rotation, ns.translation, sk, pr);
                    ns.rotation = glm::normalize(ns.rotation);
                    ns.hasTRS = true;
                }
                return ns;
            };
            ensureNS(midNode).translation *= sf;
            ensureNS(endNode).translation *= sf;
            // Recompute positions with stretched bones
            midPos = glm::vec3(compute_global(midNode, 0, states)[3]);
            endPos = glm::vec3(compute_global(endNode, 0, states)[3]);
            a = glm::length(midPos - rootPos);
            b = glm::length(endPos - midPos);
        }
    }

    // Clamp target distance to reachable range (after possible stretch)
    float c    = glm::clamp(dist, glm::abs(a - b) + 0.001f, a + b - 0.001f);

    // Law of cosines: angle at root
    float cosA = glm::clamp((a*a + c*c - b*b) / (2.0f * a * c), -1.0f, 1.0f);
    float sinA = glm::sqrt(glm::max(0.0f, 1.0f - cosA * cosA));

    // Direction from root toward target
    glm::vec3 toTarget = (dist > 1e-5f) ? glm::normalize(targetM - rootPos) : glm::vec3(0,1,0);

    // Pole perpendicular (elbow/knee bend direction)
    glm::vec3 toPole  = poleM - rootPos;
    glm::vec3 polePerp = toPole - glm::dot(toPole, toTarget) * toTarget;
    if (glm::length(polePerp) < 0.001f) {
        glm::vec3 arb = glm::abs(toTarget.y) < 0.9f ? glm::vec3(0,1,0) : glm::vec3(1,0,0);
        polePerp = glm::normalize(glm::cross(toTarget, arb));
    } else {
        polePerp = glm::normalize(polePerp);
    }

    // Desired mid (elbow/knee) position
    glm::vec3 newMidPos = rootPos + a * (toTarget * cosA + polePerp * sinA);

    // 1. Rotate root bone to align mid toward newMidPos
    rotate_bone_from_to(rootNode,
                        glm::normalize(midPos    - rootPos),
                        glm::normalize(newMidPos - rootPos),
                        states);

    // 2. After root rotation, recompute actual mid & end positions
    glm::vec3 newMidActual = glm::vec3(compute_global(midNode, 0, states)[3]);
    glm::vec3 newEndActual = glm::vec3(compute_global(endNode, 0, states)[3]);

    // 3. Rotate mid bone to align end toward target
    glm::vec3 oldMidDir = (glm::length(newEndActual - newMidActual) > 1e-5f)
                        ? glm::normalize(newEndActual - newMidActual)
                        : glm::vec3(0,-1,0);
    glm::vec3 newMidDir = (glm::length(targetM - newMidActual) > 1e-5f)
                        ? glm::normalize(targetM - newMidActual)
                        : oldMidDir;
    rotate_bone_from_to(midNode, oldMidDir, newMidDir, states);
}

// ---------------------------------------------------------------------------
// apply_spine_lean – tilt the torso forward by rotating the three spine bones.
// Mixamo GLTF: X = right, Y = up, -Z = forward in model space.
// A positive leanAngleDeg rotates the chest toward -Z (forward lean).
// We split the total angle equally across Spine / Spine1 / Spine2 so the
// curvature looks natural rather than hinging at a single joint.
// ---------------------------------------------------------------------------
void AnimationSystem::apply_spine_lean(float leanAngleDeg, std::vector<NodeState>& states)
{
    static const char* kSpineBones[] = { "Spine", "Spine1", "Spine2" };
    constexpr int      kCount = 3;

    // Distribute total lean evenly; rotate around local +X (right) axis.
    // glm::angleAxis(positive angle, +X) tilts the bone's +Y toward -Z == forward.
    float radPerBone = glm::radians(leanAngleDeg) / float(kCount);
    glm::quat leanQ  = glm::angleAxis(radPerBone, glm::vec3(1.0f, 0.0f, 0.0f));

    for (const char* boneName : kSpineBones) {
        int nodeIdx = find_node_by_name(boneName);
        if (nodeIdx < 0) continue;

        NodeState& ns = states[nodeIdx];
        if (!ns.hasTRS) {
            glm::vec3 skew; glm::vec4 persp;
            glm::decompose(mNodes[nodeIdx].localTransform,
                           ns.scale, ns.rotation, ns.translation, skew, persp);
            ns.rotation = glm::normalize(ns.rotation);
            ns.hasTRS   = true;
        }
        // Post-multiply so the lean is applied in the bone's own local frame.
        ns.rotation = glm::normalize(ns.rotation * leanQ);
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
        .each([&](flecs::entity e, AnimationComponent& ac, SkinComponent& sc)
    {
        // Advance animation time only when a valid clip is assigned
        if (ac.playing && ac.animIndex >= 0 && ac.animIndex < (int)mAnims.size()) {
            const EngineAnimation& anim = mAnims[ac.animIndex];
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
        }

        // Optional IK: pick up RiderIKComponent and WorldTransform if present
        const RiderIKComponent* rik = nullptr;
        glm::mat4 charWorld = glm::mat4(1.0f);
        if (e.has<RiderIKComponent>()) {
            rik = &e.get<RiderIKComponent>();
            if (e.has<WorldTransform>())
                charWorld = e.get<WorldTransform>().matrix;
        }

        // Compute bone matrices (animIndex -1 = rest pose; IK still applied)
        evaluate_skin(sc.skinIndex, ac.animIndex, ac.currentTime, sc.boneMatrices, rik, charWorld);
    });
}

} // namespace engine
