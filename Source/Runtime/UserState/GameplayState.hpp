#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

// Bike tuning parameters — exposed to gameplay
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

	enum class GameFlowState
	{
		MainMenu,
		Playing,
		Paused,
		Settings,
		GameOver,
		Victory
	};

	struct GameplayState
	{
		//================Mouse================================
		float mouseX = 0.f;
		float mouseY = 0.f;
		float mouseLastX = 0.f;
		float mouseLastY = 0.f;
		bool previousMouseState = false;
		bool wasMousing = false;

		//================Camera================================
		float Yaw = 0.f;
		float Pitch = 0.f;
		float Distance = 0.f;

		glm::mat4 camera2world = glm::identity<glm::mat4>();

		float cameraFov = 85.0f;
		float targetFov = 85.0f;
		float targetYaw = 0.f;
		float targetPitch = 0.f;
		float targetDistance = 5.0f;

		float cameraIdleTimer = 0.0f;
		float bikeYaw = 0.0f;
		float bikeLeanAngle = 0.0f;
		float cameraRoll = 0.0f;
		float targetCameraRoll = 0.0f;

		//================Game Flow================================
		GameFlowState gameFlowState = GameFlowState::MainMenu;
		bool isGameStarted = true;
		bool isGameOver = false;
		bool isGameWon = false;
		bool isGamePause = false;
		bool isExtremeSpeed = false;

		//================Player================================
		glm::vec3 followTargetPos = glm::vec3(2.f);
		bool thirdPersonMode = true;
		bool isAlive = true;

		bool jumpEnabled = true;
		bool hornEnabled = false;
		bool radioEnabled = false;
		bool showHints = true;
		float deathFactor = 0.0f;
		float deathTimer = 0.0f;
		float bikeSpeed = 0.0f;
		float bikeSteerAngle = 0.0f;
		float engineForce = 0.0f;
		int lastPedal = -1;
		int deathCount = 0;
		bool restartRequested = false;
		bool returnToMainMenuRequested = false;

		BikeTuning bikeTuning{};

		//================Collectibles================================
		int  collectedItems  = 0;
		int  totalCollectibles = 15;
		bool allCollected    = false;
		bool radioMuted = false;
		//================Graphics toggles (game-controlled)================================
		bool iblEnabled = true;
		bool bloomEnabled = true;
		bool ssrEnabled = true;
		bool ssaoEnabled = true;

		bool showRuntimeUi = true;
	};

} // namespace engine
