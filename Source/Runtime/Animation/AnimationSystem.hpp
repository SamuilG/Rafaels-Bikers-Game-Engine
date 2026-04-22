#pragma once

#include "../Core/System.h"
#include "../Scene/model_loader/engine_model.hpp"
#include <vector>
#include <string>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

// Forward declare to avoid circular include
namespace engine { class SceneManager; }


// One 2-bone IK chain (shoulder→elbow→hand or hip→knee→foot)
struct IKChainConfig {
    std::string rootBone;               // e.g. "upper_arm.L"
    std::string midBone;                // e.g. "forearm.L"
    std::string endBone;                // e.g. "hand.L"
    glm::vec3   localBikeTarget{0,0,0}; // target position in bike-local space (fallback)
    glm::vec3   localBikePole  {0,1,0}; // pole/elbow hint in bike-local space
    bool        enabled = true;

    // If set, the IK target is derived from this specific bike-part entity's
    // LocalTransform + localTargetOffset (e.g. steer, pedal).
    // Takes priority over localBikeTarget when non-zero.
    uint64_t  targetEntityId   = 0;
    glm::vec3 localTargetOffset{0,0,0}; // offset inside the target entity's local space

    // Filled by SceneManager each frame before AnimationSystem runs
    glm::vec3 worldTarget{0,0,0};
    glm::vec3 worldPole  {0,1,0};
};

// Rider IK component – attach to the character entity
struct RiderIKComponent {
    uint64_t bikeEntityId = 0;
    std::vector<IKChainConfig> chains;

    // Forward lean: degrees the spine tilts toward the handlebars (positive = lean forward).
    // Split evenly across Spine / Spine1 / Spine2 bones before IK is solved.
    float leanAngleDeg = 30.0f;
};

namespace engine {

class AnimationSystem final : public System {
public:
    AnimationSystem() = default;
    ~AnimationSystem() override { Shutdown(); }

    void Init()     override {}
    void Update(float dt) override;
    void Shutdown() override {}

    // Provide the SceneManager so this system can query/modify ECS components
    void set_scene_manager(SceneManager* sm) { mSceneManager = sm; }

    // Register animation data from a loaded model.
    // Returns the base indices so callers can assign animIndex / skinIndex to entities.
    struct Registration {
        uint32_t baseSkinIndex;
        uint32_t baseAnimIndex;
        uint32_t baseNodeIndex;
    };
    Registration register_model(const EngineModel& model);

    // Number of registered skins / animations
    size_t skin_count()  const { return mSkins.size(); }
    size_t anim_count()  const { return mAnims.size(); }
    size_t node_count()  const { return mNodes.size(); }

    // Find a node by name; returns -1 if not found
    int find_node_by_name(const std::string& name) const;

    // Debug: print all registered node names (call after loading to discover bone names)
    void debug_print_nodes() const;

private:
    SceneManager* mSceneManager = nullptr;

    // Global flat arrays (all registered models merged here)
    std::vector<EngineNode>      mNodes;
    std::vector<EngineSkin>      mSkins;
    std::vector<EngineAnimation> mAnims;

    // Per-node base index offsets (to remap node indices across models)
    std::vector<uint32_t> mNodeBasePerSkin;  // mNodeBasePerSkin[skinIdx] = node base for that skin's model

    // Animated local TRS state for one node (overrides the default transform)
    struct NodeState {
        glm::vec3 translation = glm::vec3(0.0f);
        glm::quat rotation    = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
        glm::vec3 scale       = glm::vec3(1.0f);
        bool hasTRS = false; // true once any channel has written to this node
    };

    // Sample a keyframe sampler at time t (returns vec4; xyz for T/S, xyzw for R)
    glm::vec4 sample_sampler(const EngineAnimSampler& s, float t, bool isRotation) const;

    // Recursively compute the global matrix of a node using animated states
    glm::mat4 compute_global(int nodeIdx, uint32_t nodeBase,
                             const std::vector<NodeState>& states) const;

    // Evaluate a skin at time t using the given animation; writes into outMatrices
    // Optionally applies IK chains (pass rik + character world transform)
    void evaluate_skin(int skinIdx, int animIdx, float t,
                       std::vector<glm::mat4>& outMatrices,
                       const RiderIKComponent* rik = nullptr,
                       const glm::mat4& charWorld = glm::mat4(1.0f));

    // --- IK helpers ---
    // Rotate nodeIdx so its child direction rotates from oldDir to newDir (model space)
    void rotate_bone_from_to(int nodeIdx,
                             const glm::vec3& oldDir, const glm::vec3& newDir,
                             std::vector<NodeState>& states);

    // Apply one 2-bone analytic IK chain; modifies states in place
    void apply_two_bone_ik(const IKChainConfig& chain,
                           const glm::mat4& worldToModel,
                           std::vector<NodeState>& states);

    // Rotate spine bones (Spine/Spine1/Spine2) by leanAngleDeg in the character's local +X axis
    void apply_spine_lean(float leanAngleDeg, std::vector<NodeState>& states);
};

} // namespace engine
