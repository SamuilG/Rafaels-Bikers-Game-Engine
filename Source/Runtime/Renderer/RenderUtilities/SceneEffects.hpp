#pragma once

#include <cstdint>
#include <limits>
#include <glm/glm.hpp>

namespace engine {

// Rendering consumes geometric instructions, without knowing which gameplay
// object crossed a portal or which rules produced a post-process effect.
struct PortalTransitionVisual {
    bool active = false;
    std::uint32_t bodyId = std::numeric_limits<std::uint32_t>::max();
    std::uint64_t entityId = 0;
    glm::vec4 realClipPlane = glm::vec4(0.0f);
    glm::vec4 cloneClipPlane = glm::vec4(0.0f);
    glm::mat4 cloneMap = glm::mat4(1.0f);
};

struct SceneEffects {
    float speedFactor = 0.0f;
    bool subjectAlive = true;
    float deathFactor = 0.0f;
    float portalEffectTime = 0.0f;
    PortalTransitionVisual portalTransition;
};

} // namespace engine
