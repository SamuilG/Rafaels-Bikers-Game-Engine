#pragma once

#include "DebugRenderer.hpp"

#include <glm/glm.hpp>
#include <Jolt/Jolt.h>
#include <Jolt/Physics/Collision/TransformedShape.h>

namespace engine::physics_debug {

    void DrawSelectionBounds(
        DebugRenderer& debugRenderer,
        const JPH::TransformedShape& transformedShape,
        const glm::vec3& color
    );

    void DrawCollisionShapeWireframe(
        DebugRenderer& debugRenderer,
        const JPH::TransformedShape& transformedShape,
        const glm::vec3& color
    );

} // namespace engine::physics_debug
