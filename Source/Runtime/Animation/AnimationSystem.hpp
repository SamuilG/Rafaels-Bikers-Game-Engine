#pragma once

#include "../Core/System.h"
#include "../Scene/model_loader/engine_model.hpp"
#include <vector>
#include <glm/gtc/quaternion.hpp>

// Forward declare to avoid circular include
namespace engine { class SceneManager; }

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
    void evaluate_skin(int skinIdx, int animIdx, float t,
                       std::vector<glm::mat4>& outMatrices);
};

} // namespace engine
