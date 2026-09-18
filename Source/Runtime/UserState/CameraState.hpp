#pragma once
#include <glm/glm.hpp>

namespace engine {
enum class CameraMode { Follow, Free, Portal, Cinematic };

// Published camera snapshot. CameraController exposes this only by const reference.
struct CameraState {
    CameraMode mode = CameraMode::Follow;
    bool thirdPersonMode = true;
    bool automaticFov = true;
    glm::mat4 camera2world = glm::mat4(glm::vec4(1,0,0,0), glm::vec4(0,1,0,0),
        glm::vec4(0,0,1,0), glm::vec4(0,2,10,1));
    float cameraFov = 85.0f;
    float targetFov = 85.0f;
    float Yaw = 0.0f, Pitch = 0.0f, Distance = 5.0f;
    float targetYaw = 0.0f, targetPitch = 0.0f, targetDistance = 5.0f;
    float cameraIdleTimer = 0.0f;
    float cameraRoll = 0.0f, targetCameraRoll = 0.0f;
    glm::vec3 followTargetPos = glm::vec3(2.0f);
    bool portalCameraActive = false;
    float portalCameraTimer = 0.0f;
    float portalCameraBoomLength = 0.0f;
    float portalCameraStartSide = 1.0f;
    glm::vec3 portalCameraPosition = glm::vec3(0.0f);
    glm::vec3 portalCameraTargetPosition = glm::vec3(0.0f);
    glm::vec3 portalCameraBoomOffset = glm::vec3(0.0f);
    glm::mat4 portalCameraEntrySurface = glm::mat4(1.0f);
    glm::mat4 portalCameraExitSurface = glm::mat4(1.0f);
    glm::mat4 portalCameraInverseExitSurface = glm::mat4(1.0f);
};

struct CameraInput {
    glm::vec2 lookDelta = glm::vec2(0.0f); // radians this frame
    glm::vec3 localMove = glm::vec3(0.0f); // units per second
    float distanceDelta = 0.0f;
    float fovDelta = 0.0f; // degrees this frame
    bool holdAutoAlign = false;
    bool allowFreeMovement = true;
};

struct PortalCameraRequest {
    glm::mat4 entrySurface = glm::mat4(1.0f);
    glm::mat4 exitSurface = glm::mat4(1.0f);
    glm::mat4 portalMap = glm::mat4(1.0f);
    glm::vec3 exitCorrection = glm::vec3(0.0f);
    glm::vec3 mappedFollowTarget = glm::vec3(0.0f);
    bool teleportImmediately = false;
};
} // namespace engine
