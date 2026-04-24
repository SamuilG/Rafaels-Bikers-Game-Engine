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
        float planeLength = glm::length(glm::vec3(planeEquation));
        if (planeLength <= 0.0001f) {
            return {};
        }

        glm::vec4 normalizedPlane = planeEquation / planeLength;
        return { glm::vec3(normalizedPlane), normalizedPlane.w };
    }

    inline Frustum BuildFrustum(const glm::mat4& viewProjection)
    {
        //transpose first so m[i] becomes the i-th row of the matrix.
        glm::mat4 m = glm::transpose(viewProjection);

        Frustum frustum{};
        frustum.planes[0] = NormalizeFrustumPlane(m[3] + m[0]); // left
        frustum.planes[1] = NormalizeFrustumPlane(m[3] - m[0]); //  right
        frustum.planes[2] = NormalizeFrustumPlane(m[3] + m[1]); //bottom
        frustum.planes[3] = NormalizeFrustumPlane(m[3] - m[1]); // top
        frustum.planes[4] = NormalizeFrustumPlane(m[3] + m[2]); // near
        frustum.planes[5] = NormalizeFrustumPlane(m[3] - m[2]); //  far
        return frustum;
    }

    inline void TransformAabb(
        const glm::vec3& localMin,
        const glm::vec3& localMax,
        const glm::mat4& transform,
        glm::vec3& outWorldMin,
        glm::vec3& outWorldMax)
    {
        //convert the local mesh AABB into a world-space AABB.
        glm::vec3 localCenter = (localMin + localMax) * 0.5f;
        glm::vec3 localExtents = (localMax - localMin) * 0.5f;

        glm::vec3 worldCenter = glm::vec3(transform * glm::vec4(localCenter, 1.0f));
        glm::mat3 linearPart = glm::mat3(transform);
        glm::mat3 absLinearPart(
            glm::abs(linearPart[0]),
            glm::abs(linearPart[1]),
            glm::abs(linearPart[2])
        );
        glm::vec3 worldExtents = absLinearPart * localExtents;

        outWorldMin = worldCenter - worldExtents;
        outWorldMax = worldCenter + worldExtents;
    }

    inline bool IntersectsAabb(const Frustum& frustum, const glm::vec3& worldMin, const glm::vec3& worldMax, float padding = 0.0f)
    {
        for (const FrustumPlane& plane : frustum.planes) {
            glm::vec3 positiveVertex = worldMin;

            if (plane.normal.x >= 0.0f) positiveVertex.x = worldMax.x;
            if (plane.normal.y >= 0.0f) positiveVertex.y = worldMax.y;
            if (plane.normal.z >= 0.0f) positiveVertex.z = worldMax.z;

            //if the most positive vertex is still outside, the whole AABB is outside.
            if (glm::dot(plane.normal, positiveVertex) + plane.distance < -padding) {
                return false;
            }
        }

        return true;
    }

} // namespace engine
