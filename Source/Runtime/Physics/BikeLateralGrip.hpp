#pragma once

#include <cmath>

namespace engine {

    inline float CalculateBikeLateralGripImpulse(float lateralSpeed, float inverseMass, float dt) {
        if (dt <= 0.0f || inverseMass <= 0.0f) {
            return 0.0f;
        }

        // Integrate F = -k * v analytically so a long frame cannot reverse
        // the lateral velocity. Use the current mass, including pickup changes.
        constexpr float kLateralGripStiffness = 5000.0f;
        const float removedFraction = -std::expm1(-kLateralGripStiffness * inverseMass * dt);
        return -lateralSpeed * removedFraction / inverseMass;
    }

} // namespace engine
