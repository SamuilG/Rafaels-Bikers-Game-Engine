#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/matrix_decompose.hpp>
#include <optional>
#include <cmath>

namespace engine {
struct PhysicsTransform {
    glm::vec3 scale, position;
    glm::quat rotation;
};
inline std::optional<PhysicsTransform> DecomposePhysicsTransform(const glm::mat4& matrix) {
    for (int col = 0; col < 4; ++col) for (int row = 0; row < 4; ++row)
        if (!std::isfinite(matrix[col][row])) return std::nullopt;
    glm::dvec3 scale, position, skew; glm::dquat rotation; glm::dvec4 perspective;
    // A group's scale multiplies the GLB node scale. Float decomposition treats
    // legitimate small meshes as singular and leaves its outputs uninitialized.
    if (!glm::decompose(glm::dmat4(matrix), scale, rotation, position, skew, perspective)) return std::nullopt;
    for (int axis = 0; axis < 3; ++axis)
        if (!std::isfinite(scale[axis]) || !std::isfinite(position[axis]) || std::abs(scale[axis]) < 1e-6)
            return std::nullopt;
    if (!std::isfinite(glm::length(rotation)) || glm::length(rotation) < 1e-12) return std::nullopt;
    return PhysicsTransform{glm::vec3(scale), glm::vec3(position), glm::quat(glm::normalize(rotation))};
}
}
