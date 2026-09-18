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
struct LevelState {
    bool portalTransitionVisualActive = false;
    float portalTransitionVisualTimer = 0.0f;
    float portalTransitionVisualDuration = 0.35f;
    bool portalTransitionRealAtExit = false;
    glm::mat4 portalTransitionEntrySurface = glm::identity<glm::mat4>();
    glm::mat4 portalTransitionExitSurface = glm::identity<glm::mat4>();
    glm::vec3 portalTransitionExitCorrection = glm::vec3(0.0f);
    int  collectedItems  = 0;
    int  totalCollectibles = 15;
    bool allCollected    = false;
    bool radioMuted = false;
    BikeTuning bikeTuning{};
};
}
