#pragma once

#ifndef GLM_ENABLE_EXPERIMENTAL
#define GLM_ENABLE_EXPERIMENTAL
#endif

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/matrix_decompose.hpp>

#include <cmath>
#include <limits>

namespace engine::editor_transform
{
// Returns signed TRS components compatible with ImGuizmo's XYZ recomposition.
// Scale signs are not unique: a reflection may become three negative axes and
// an equivalent rotation. Preserve the matrix, rather than the original signs.
// Unsupported (singular, non-finite, sheared or projective) matrices leave all
// output arrays unchanged, so a failed refresh cannot poison the editor cache.
inline bool Decompose(const glm::mat4& matrix, float translation[3],
                      float rotationDegrees[3], float scale[3])
{
    constexpr double affineTolerance = 1e-6;
    constexpr double shearTolerance = 1e-5;
    for (int column = 0; column < 4; ++column)
        for (int row = 0; row < 4; ++row)
            if (!std::isfinite(matrix[column][row]))
                return false;

    for (int column = 0; column < 3; ++column)
        if (std::abs(matrix[column][3]) > affineTolerance)
            return false;
    if (std::abs(matrix[3][3] - 1.0) > affineTolerance)
        return false;

    // Use double precision internally, particularly near an Euler singularity.
    glm::dvec3 position, signedScale, skew;
    glm::dvec4 perspective;
    glm::dquat orientation;
    if (!glm::decompose(glm::dmat4(matrix), signedScale, orientation,
                        position, skew, perspective))
        return false;

    for (int axis = 0; axis < 3; ++axis)
    {
        if (!std::isfinite(signedScale[axis]) ||
            std::abs(signedScale[axis]) < std::numeric_limits<float>::epsilon() ||
            !std::isfinite(skew[axis]) || std::abs(skew[axis]) > shearTolerance)
            return false;
    }
    const double quaternionLength = glm::length(orientation);
    if (!std::isfinite(quaternionLength) || quaternionLength <= 0.0)
        return false;
    const glm::dmat3 rotation = glm::mat3_cast(orientation / quaternionLength);

    // ImGuizmo composes column-vector transforms as Rz * Ry * Rx. At +/-90
    // degrees around Y, choose Z = 0 and keep the combined X/Z rotation in X.
    const double cosY = std::hypot(rotation[0][0], rotation[0][1]);
    glm::dvec3 angles;
    angles.y = std::atan2(-rotation[0][2], cosY);
    if (cosY > 1e-6)
    {
        angles.x = std::atan2(rotation[1][2], rotation[2][2]);
        angles.z = std::atan2(rotation[0][1], rotation[0][0]);
    }
    else
    {
        angles.x = std::atan2(-rotation[2][1], rotation[1][1]);
        angles.z = 0.0;
    }
    angles = glm::degrees(angles);

    float newTranslation[3], newRotation[3], newScale[3];
    for (int axis = 0; axis < 3; ++axis)
    {
        newTranslation[axis] = static_cast<float>(position[axis]);
        newRotation[axis] = static_cast<float>(angles[axis]);
        newScale[axis] = static_cast<float>(signedScale[axis]);
        if (!std::isfinite(newTranslation[axis]) ||
            !std::isfinite(newRotation[axis]) || !std::isfinite(newScale[axis]))
            return false;
    }
    for (int axis = 0; axis < 3; ++axis)
    {
        translation[axis] = newTranslation[axis];
        rotationDegrees[axis] = newRotation[axis];
        scale[axis] = newScale[axis];
    }
    return true;
}
}
