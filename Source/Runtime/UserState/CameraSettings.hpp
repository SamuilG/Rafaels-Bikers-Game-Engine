#pragma once
#include "CameraState.hpp"
#include <algorithm>
#include <cmath>

namespace engine {

// Scene-authored camera configuration. Keep this separate from CameraState:
// movement, smoothing and temporary gameplay takeovers must not edit the scene.
struct CameraSettings {
    CameraMode mode = CameraMode::Follow;
    bool automaticFov = true;
    float fov = 85.0f; // vertical field of view in degrees
    float nearPlane = 0.1f;
    float farPlane = 1000.0f;
    float yaw = 0.0f; // orbit angles in radians
    float pitch = 0.0f;
    float distance = 5.0f;
    bool occlusionFadeEnabled = true;
    float occlusionFadeCoverage = 0.3f;
    float occlusionFadeSpeed = 8.0f;
    float occlusionTargetHeight = 0.8f;

    bool operator==(const CameraSettings&) const = default;

    bool IsValid() const {
        return (mode == CameraMode::Follow || mode == CameraMode::Free) &&
            std::isfinite(fov) && fov >= 10.0f && fov <= 120.0f &&
            std::isfinite(nearPlane) && nearPlane >= 0.001f &&
            std::isfinite(farPlane) && farPlane > nearPlane && farPlane <= 1000000.0f &&
            std::isfinite(yaw) && yaw >= glm::radians(-180.0f) && yaw <= glm::radians(180.0f) &&
            std::isfinite(pitch) &&
            pitch >= glm::radians(-85.0f) && pitch <= glm::radians(85.0f) &&
            std::isfinite(distance) && distance >= 2.0f && distance <= 70.0f &&
            std::isfinite(occlusionFadeCoverage) && occlusionFadeCoverage >= 0.0f && occlusionFadeCoverage <= 1.0f &&
            std::isfinite(occlusionFadeSpeed) && occlusionFadeSpeed >= 0.1f && occlusionFadeSpeed <= 30.0f &&
            std::isfinite(occlusionTargetHeight) && occlusionTargetHeight >= 0.0f && occlusionTargetHeight <= 3.0f;
    }

    // Initial migration only; never copy a simulated snapshot back every frame.
    static CameraSettings FromState(const CameraState& state) {
        CameraSettings settings;
        settings.mode = state.mode == CameraMode::Free ? CameraMode::Free : CameraMode::Follow;
        settings.automaticFov = state.automaticFov;
        settings.fov = state.targetFov;
        settings.nearPlane = state.nearPlane;
        settings.farPlane = state.farPlane;
        settings.yaw = std::remainder(state.targetYaw, glm::radians(360.0f));
        settings.pitch = std::clamp(state.targetPitch, glm::radians(-85.0f), glm::radians(85.0f));
        settings.distance = std::clamp(state.targetDistance, 2.0f, 70.0f);
        settings.occlusionFadeEnabled = state.occlusionFadeEnabled;
        settings.occlusionFadeCoverage = state.occlusionFadeCoverage;
        settings.occlusionFadeSpeed = state.occlusionFadeSpeed;
        settings.occlusionTargetHeight = state.occlusionTargetHeight;
        return settings;
    }
};

} // namespace engine
