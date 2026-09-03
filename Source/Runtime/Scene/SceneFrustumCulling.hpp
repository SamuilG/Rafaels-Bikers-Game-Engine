#pragma once

#include <glm/glm.hpp>

namespace engine {

    struct FrustumPlane {
        glm::vec3 normal = glm::vec3(0.0f);
        float distance = 0.0f;
    };

    struct Frustum {
        FrustumPlane planes[6];
    };

    inline FrustumPlane NormalizeFrustumPlane(const glm::vec4& planeEquation)
    {
        const float planeLength = glm::length(glm::vec3(planeEquation));
        if (planeLength <= 0.0001f) return {};

        const glm::vec4 normalizedPlane = planeEquation / planeLength;
        return { glm::vec3(normalizedPlane), normalizedPlane.w };
    }

    inline Frustum BuildFrustum(const glm::mat4& viewProjection)
    {
        const glm::mat4 rows = glm::transpose(viewProjection);
        Frustum frustum{};
        frustum.planes[0] = NormalizeFrustumPlane(rows[3] + rows[0]);
        frustum.planes[1] = NormalizeFrustumPlane(rows[3] - rows[0]);
        frustum.planes[2] = NormalizeFrustumPlane(rows[3] + rows[1]);
        frustum.planes[3] = NormalizeFrustumPlane(rows[3] - rows[1]);
        frustum.planes[4] = NormalizeFrustumPlane(rows[3] + rows[2]);
        frustum.planes[5] = NormalizeFrustumPlane(rows[3] - rows[2]);
        return frustum;
    }

    inline void TransformAabb(
        const glm::vec3& localMin,
        const glm::vec3& localMax,
        const glm::mat4& transform,
        glm::vec3& outWorldMin,
        glm::vec3& outWorldMax)
    {
        const glm::vec3 localCenter = (localMin + localMax) * 0.5f;
        const glm::vec3 localExtents = (localMax - localMin) * 0.5f;
        const glm::vec3 worldCenter = glm::vec3(transform * glm::vec4(localCenter, 1.0f));
        const glm::mat3 linearPart = glm::mat3(transform);
        const glm::mat3 absLinearPart(
            glm::abs(linearPart[0]), glm::abs(linearPart[1]), glm::abs(linearPart[2]));
        const glm::vec3 worldExtents = absLinearPart * localExtents;
        outWorldMin = worldCenter - worldExtents;
        outWorldMax = worldCenter + worldExtents;
    }

    inline bool IntersectsAabb(
        const Frustum& frustum,
        const glm::vec3& worldMin,
        const glm::vec3& worldMax,
        float padding = 0.0f)
    {
        for (const FrustumPlane& plane : frustum.planes) {
            glm::vec3 positiveVertex = worldMin;
            if (plane.normal.x >= 0.0f) positiveVertex.x = worldMax.x;
            if (plane.normal.y >= 0.0f) positiveVertex.y = worldMax.y;
            if (plane.normal.z >= 0.0f) positiveVertex.z = worldMax.z;
            if (glm::dot(plane.normal, positiveVertex) + plane.distance < -padding) return false;
        }
        return true;
    }

} // namespace engine
