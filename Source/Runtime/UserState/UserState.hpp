#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

struct BikeTuning
{
    float maxSteerAngleDeg = 25.0f;
    float steerSpeedDeg = 90.0f;
    float maxLeanAngleDeg = 30.0f;
    float leanSpeedDeg = 90.0f;
    float wheelBase = 1.6f;
    float driveForce = 1000.0f;
    float brakeForce = 20.0f;
    float maxSpeed = 120.0f;
    float gravityFactor = 100.0f;
};

namespace engine {
    struct GameplayState
    {
        bool isGameStarted = true;
        bool isGameOver = false;
        bool isGamePause = false;

        bool isAlive = true;
        int remainingLives = 3;
        int maxLives = 3;

        int collectedItems = 0;
        int totalCollectibles = 15;
        bool allCollected = false;
    };

    struct CameraState
    {
        float Yaw = 0.f;
        float Pitch = glm::radians(10.0f);
        float Distance = 6.5f;

        glm::mat4 camera2world = glm::identity<glm::mat4>();

        float cameraFov = 85.0f;
        float targetFov = 85.0f;
        float targetYaw = 0.f;
        float targetPitch = glm::radians(10.0f);
        float targetDistance = 6.5f;

        float cameraIdleTimer = 0.0f;
        float bikeYaw = 0.0f;
        float cameraRoll = 0.0f;
        float targetCameraRoll = 0.0f;
    };

    struct UserState
    {
        UserState()
            : Yaw(camera.Yaw)
            , Pitch(camera.Pitch)
            , Distance(camera.Distance)
            , camera2world(camera.camera2world)
            , cameraFov(camera.cameraFov)
            , targetFov(camera.targetFov)
            , targetYaw(camera.targetYaw)
            , targetPitch(camera.targetPitch)
            , targetDistance(camera.targetDistance)
            , cameraIdleTimer(camera.cameraIdleTimer)
            , bikeYaw(camera.bikeYaw)
            , cameraRoll(camera.cameraRoll)
            , targetCameraRoll(camera.targetCameraRoll)
            , isGameStarted(gameplay.isGameStarted)
            , isGameOver(gameplay.isGameOver)
            , isGamePause(gameplay.isGamePause)
            , isAlive(gameplay.isAlive)
            , remainingLives(gameplay.remainingLives)
            , maxLives(gameplay.maxLives)
            , collectedItems(gameplay.collectedItems)
            , totalCollectibles(gameplay.totalCollectibles)
            , allCollected(gameplay.allCollected) {
        }

        float mouseX = 0.f;
        float mouseY = 0.f;

        float mouseLastX = 0.f;
        float mouseLastY = 0.f;

        bool previousMouseState = false;
        bool wasMousing = false;

        CameraState camera{};
        GameplayState gameplay{};

        // Transitional aliases: existing call sites can keep compiling while code migrates to camera/gameplay.
        float& Yaw;
        float& Pitch;
        float& Distance;
        glm::mat4& camera2world;
        float& cameraFov;
        float& targetFov;
        float& targetYaw;
        float& targetPitch;
        float& targetDistance;
        float& cameraIdleTimer;
        float& bikeYaw;
        float& cameraRoll;
        float& targetCameraRoll;

        bool& isGameStarted;
        bool& isGameOver;
        bool& isGamePause;
        bool& isAlive;
        int& remainingLives;
        int& maxLives;
        int& collectedItems;
        int& totalCollectibles;
        bool& allCollected;

        int renderMode = 0;

        bool showEngineUi = true;
        bool particlesEnabled = true;

        bool showControlPanel = true;
        bool showContentBrowser = true;
        bool showSceneHierarchy = true;
        bool showEntityInspector = true;
        bool showConsole = true;
        bool showLightPanel = true;
        bool showCameraPanel = true;
        bool showDebugPanel = true;
        bool showAudioPanel = true;

        bool debugSelectionBounds = false;
        bool debugCollisionShapes = true;
        bool frustumCullingEnabled = false;

        float frustumCullingOffFps = 0.0f;
        float frustumCullingOnFps = 0.0f;
        uint32_t frustumCullingTotalCandidates = 0;
        uint32_t frustumCullingVisibleCandidates = 0;
        float frustumCullingPadding = 0.5f;

        bool lodEnabled = true;
        float lodDebugDistance = -1.0f;

        bool isExtremeSpeed = false;

        BikeTuning bikeTuning{};
        int activeParticleIndex = -1;
        bool isSceneViewportHovered = false;

        glm::vec3 followTargetPos = glm::vec3(2.f);
        bool thirdPersonMode = true;
        float deathFactor = 0.0f;
        float deathTimer = 0.0f;
        float bikeSpeed = 0.0f;
        float bikeSteerAngle = 0.0f;

        bool bloomEnabled = false;
        bool iblEnabled = true;
        bool stereoPreviewEnabled = true;

        bool mosaicEnabled = false;
        bool ssrEnabled = true;
        bool ssaoEnabled = true;
        float bloomExposure = 1.0f;
        float bloomStrength = 1.2f;
        float bloomThreshold = 1.0f;
        int bloomKernelRadius = 7;
        bool bloomUseACES = true;

        float bikeLeanAngle = 0.0f;
    };
} // namespace engine
