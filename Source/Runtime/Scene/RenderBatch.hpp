#pragma once

#include <cstdint>
#include <glm/glm.hpp>

// CPU draw contract shared by scene preparation and GPU recording. It carries
// opaque IDs for grouping/attachment and has no ECS, physics, or game headers.
struct RenderBatch {
    std::uint32_t meshIndex;
    std::uint32_t materialIndex;
    glm::mat4 transform;
    // Fraction retained by the opaque dither-fade pass (not alpha blending).
    float ditherFade = 1.0f;
    std::uint64_t entityId = 0;
    std::uint32_t compoundBodyID = UINT32_MAX;
    bool castShadow = true;
    bool isSkinned = false;
    std::uint32_t boneBaseIndex = 0;
    std::uint64_t attachedEntityId = 0;
    glm::vec4 clipPlane = glm::vec4(0.0f);
    bool isPortalClone = false;
};
