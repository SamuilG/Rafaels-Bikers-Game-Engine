#pragma once

#include <cstdint>
#include <glm/glm.hpp>

namespace engine {

// Published player data. Only PlayerController owns a mutable instance.
struct PlayerState {
    bool isAlive = true;
    bool controlEnabled = true;
    bool jumpEnabled = false;
    bool hornEnabled = false;
    bool radioEnabled = false;

    // Horizontal speed in metres per second; angles are in radians.
    float bikeSpeed = 0.0f;
    float bikeYaw = 0.0f;
    float bikeSteerAngle = 0.0f;
    float bikeLeanAngle = 0.0f;
    bool isExtremeSpeed = false;
    glm::vec3 position = glm::vec3(0.0f);

    int deathCount = 0;
    float deathTimer = 0.0f;
    float deathFactor = 0.0f;

    // The vehicle observes this revision to reset private pedal/steering history.
    std::uint64_t motionResetRevision = 0;
};

} // namespace engine
