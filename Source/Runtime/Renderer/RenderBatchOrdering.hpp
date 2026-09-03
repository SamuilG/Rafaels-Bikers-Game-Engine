#pragma once

#include <glm/glm.hpp>
#include "../Scene/RenderSnapshot.hpp"

namespace engine::rendering {

    struct RenderOrderingInput {
        RenderableAssetId renderableAssetId = kInvalidRenderableAssetId;
        glm::mat4 worldTransform = glm::mat4(1.0f);
        float opacity = 1.0f;
        bool materialAlphaBlend = false;
    };

    inline bool IsTransparent(const RenderOrderingInput& input)
    {
        return input.opacity < 0.999f || input.materialAlphaBlend;
    }

    inline bool DrawsBefore(
        const RenderOrderingInput& left,
        const RenderOrderingInput& right,
        const glm::vec3& cameraPosition)
    {
        const bool leftTransparent = IsTransparent(left);
        const bool rightTransparent = IsTransparent(right);
        if (leftTransparent != rightTransparent) return !leftTransparent;
        if (!leftTransparent) return left.renderableAssetId < right.renderableAssetId;

        const glm::vec3 leftDelta = glm::vec3(left.worldTransform[3]) - cameraPosition;
        const glm::vec3 rightDelta = glm::vec3(right.worldTransform[3]) - cameraPosition;
        const float leftDistance = glm::dot(leftDelta, leftDelta);
        const float rightDistance = glm::dot(rightDelta, rightDelta);
        return leftDistance > rightDistance;
    }

} // namespace engine::rendering
