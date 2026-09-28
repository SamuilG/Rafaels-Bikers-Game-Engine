#pragma once

#include "HudData.hpp"
#include "../UserState/FrameExecution.hpp"
#include "../UserState/PlayerState.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace engine {

// The game publishes one HUD snapshot after physics, independent of rendering.
class HudController {
public:
    const HudData& State() const { return mState; }

    void ResetForNewRun() {
        mState = HudData{};
        mElapsedSeconds = 0.0;
        mDistanceMeters = 0.0;
    }

    void Update(float dt, const PlayerState& player, const FrameExecution& frame) {
        if (!frame.runSimulationSystems) return;

        const float speedMps = std::isfinite(player.bikeSpeed) ? std::abs(player.bikeSpeed) : 0.0f;
        mState.speedKmh = speedMps * 3.6f;
        mState.speedMph = mState.speedKmh * 0.621371f;

        // Preserve the demo's speed-derived presentation values. These are not
        // a stamina simulation or a completed race/lap-recording system.
        mState.energy = std::clamp(1.0f - mState.speedKmh / 60.0f, 0.0f, 1.0f);
        mState.stamina = mState.energy;
        mState.isLowEnergy = mState.energy <= 0.25f;
        mState.warningColor = mState.isLowEnergy
            ? glm::vec4(1.0f, 0.26f, 0.22f, 1.0f)
            : glm::vec4(1.0f, 1.0f, 1.0f, 0.18f);

        if (std::isfinite(dt) && dt > 0.0f) {
            mElapsedSeconds += static_cast<double>(dt);
            mDistanceMeters += static_cast<double>(speedMps) * dt;
        }
        mState.distanceMeters = static_cast<float>(mDistanceMeters);
        mState.lapTimeSeconds = static_cast<float>(mElapsedSeconds);
        mState.bestLapTimeSeconds = mState.lapTimeSeconds;
        mState.currentLap = 1;
        mState.lapTime = FormatTime(mElapsedSeconds);
        mState.bestLapTime = mState.lapTime;
    }

private:
    static std::string FormatTime(double seconds) {
        const auto milliseconds = static_cast<long long>(std::llround(seconds * 1000.0));
        char formatted[64]{};
        std::snprintf(formatted, sizeof(formatted), "%02lld:%02lld.%03lld",
            milliseconds / 60000, (milliseconds / 1000) % 60, milliseconds % 1000);
        return formatted;
    }

    HudData mState;
    double mElapsedSeconds = 0.0;
    double mDistanceMeters = 0.0;
};

} // namespace engine
