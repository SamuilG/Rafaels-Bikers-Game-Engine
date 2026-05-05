#pragma once

#if defined(__linux__) || defined(__linux)
#undef Convex
#undef None
#undef Success
#undef Always
#undef True
#undef False
#undef Status
#endif

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Character/CharacterVirtual.h>
#include <Jolt/Core/Reference.h>
#include <glm/glm.hpp>

// Attached to the character entity.
// active=false  -> riding mode: CharacterVirtual is dormant, RiderBinding drives position
// active=true   -> pushing mode: CharacterVirtual controls movement, Bike follows character
struct CharacterPhysicsComponent {
    JPH::Ref<JPH::CharacterVirtual> character;
    bool      active           = false;
    glm::vec3 desiredVelocity  = glm::vec3(0.0f); // set each frame by input / BikeController
};
