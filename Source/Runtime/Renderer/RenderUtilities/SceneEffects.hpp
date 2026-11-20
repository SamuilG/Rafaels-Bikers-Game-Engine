#pragma once

#include <cstdint>
#include <limits>
#include <glm/glm.hpp>

namespace engine {

inline constexpr float kPortalOpeningDuration = 1.5f;
inline constexpr float kPortalFilterMaxFade = 10.0f;
inline constexpr float kPortalWaveMaxDuration = 8.0f;

struct PortalFilterTimeline {
    float opening = 0.0f;
    float fadeElapsed = -1.0f;

    float Envelope(float fadeDuration) const {
        return opening * (1.0f - glm::smoothstep(0.0f, fadeDuration, fadeElapsed));
    }
};

struct PortalFilterVisual {
    float amount = 0.0f;
    float time = 0.0f;
};

struct PortalWaveTimeline {
    glm::vec3 origin{0.0f};
    float age = -1.0f;
};

struct PortalWaveVisual {
    glm::vec3 origin{0.0f};
    float radius = 0.0f;
    float width = 0.0f;
    float strength = 0.0f;
    float textureScale = 0.0f;
    float maxRadius = 0.0f;
    float groundLift = 0.0f;
    float groundThickness = 0.0f;
};

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
    float portalOpeningAge = -1.0f;
    PortalFilterTimeline portalFilter;
    PortalWaveTimeline portalWave;
};

} // namespace engine
