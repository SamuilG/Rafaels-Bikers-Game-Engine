#pragma once

#include <string>
#include <glm/glm.hpp>

namespace engine {

// Published gameplay presentation data. UI consumers only copy or display it.
struct HudData {
    float speedKmh = 0.0f;
    float speedMph = 0.0f;
    float energy = 1.0f;
    float stamina = 1.0f;
    bool isLowEnergy = false;
    glm::vec4 warningColor = glm::vec4(1.0f, 1.0f, 1.0f, 0.18f);
    float distanceMeters = 0.0f;
    float lapTimeSeconds = 0.0f;
    float bestLapTimeSeconds = 0.0f;
    int currentLap = 1;
    std::string lapTime = "00:00.000";
    std::string bestLapTime = "00:00.000";
};

} // namespace engine
