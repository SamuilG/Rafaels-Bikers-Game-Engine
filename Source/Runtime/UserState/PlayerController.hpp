#pragma once

#include "PlayerState.hpp"

#include <algorithm>
#include <cmath>

namespace engine {

class PlayerController {
public:
    const PlayerState& State() const { return mState; }
    bool CanControl() const { return mState.isAlive && mState.controlEnabled; }

    bool Die() {
        if (!mState.isAlive) return false;
        mState.isAlive = false;
        mState.deathTimer = 0.0f;
        mState.deathFactor = 0.0f;
        ++mState.deathCount;
        ++mState.motionResetRevision;
        return true;
    }

    bool Respawn(float yawRadians = 0.0f) {
        if (mState.isAlive) return false;
        mState.isAlive = true;
        mState.controlEnabled = true;
        mState.deathTimer = 0.0f;
        mState.deathFactor = 0.0f;
        PublishMotion(0.0f, yawRadians, 0.0f, 0.0f);
        ++mState.motionResetRevision;
        return true;
    }

    bool Respawn(float yawRadians, const glm::vec3& position) {
        if (!Respawn(yawRadians)) return false;
        SetPosition(position);
        return true;
    }

    void ResetForNewRun() {
        const auto nextRevision = mState.motionResetRevision + 1;
        mState = PlayerState{};
        mState.motionResetRevision = nextRevision;
    }

    void SetControlEnabled(bool enabled) {
        if (mState.controlEnabled == enabled) return;
        mState.controlEnabled = enabled;
        if (!enabled) ++mState.motionResetRevision;
    }

    void PublishMotion(float speedMps, float yaw, float steer, float lean) {
        mState.bikeSpeed = std::isfinite(speedMps) ? std::abs(speedMps) : 0.0f;
        mState.bikeYaw = std::isfinite(yaw) ? yaw : 0.0f;
        mState.bikeSteerAngle = std::isfinite(steer) ? steer : 0.0f;
        mState.bikeLeanAngle = std::isfinite(lean) ? lean : 0.0f;
        mState.isExtremeSpeed = mState.bikeSpeed >= 36.0f;
    }

    // The four-argument overload preserves position when only telemetry changes.
    void PublishMotion(float speedMps, float yaw, float steer, float lean, const glm::vec3& position) {
        PublishMotion(speedMps, yaw, steer, lean);
        SetPosition(position);
    }

    void NotifyTeleported(float yaw, bool preserveDriveState) {
        PublishMotion(mState.bikeSpeed, yaw, 0.0f, 0.0f);
        if (!preserveDriveState) {
            ++mState.motionResetRevision;
        }
    }

    void NotifyTeleported(float yaw) {
        NotifyTeleported(yaw, false);
    }

    void NotifyTeleported(float yaw, const glm::vec3& position, bool preserveDriveState) {
        NotifyTeleported(yaw, preserveDriveState);
        SetPosition(position);
    }

    void NotifyTeleported(float yaw, const glm::vec3& position) {
        NotifyTeleported(yaw, position, false);
    }

    void UnlockJump() { mState.jumpEnabled = true; }
    void UnlockHorn() { mState.hornEnabled = true; }
    void UnlockRadio() { mState.radioEnabled = true; }

    // Application supplies simulation time; rendering only reads the result.
    void UpdateEffects(float dt) {
        if (!std::isfinite(dt) || dt <= 0.0f) return;
        if (!mState.isAlive) {
            constexpr float duration = 8.0f;
            constexpr float peakTime = duration * 0.02f;
            mState.deathTimer = std::min(duration, mState.deathTimer + dt);
            if (mState.deathTimer <= peakTime) {
                mState.deathFactor = mState.deathTimer / peakTime;
            }
            else {
                mState.deathFactor = std::max(0.0f,
                    1.0f - (mState.deathTimer - peakTime) / (duration - peakTime));
            }
        }
        else {
            mState.deathTimer = 0.0f;
            mState.deathFactor = std::max(0.0f, mState.deathFactor - dt * 5.0f);
        }
    }

private:
    void SetPosition(const glm::vec3& position) {
        if (std::isfinite(position.x) && std::isfinite(position.y) && std::isfinite(position.z)) {
            mState.position = position;
        }
    }

    PlayerState mState;
};

} // namespace engine
