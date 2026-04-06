#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

//UI system for bike
struct BikeTuning
{
	float maxSteerAngleDeg = 25.0f;
	float steerSpeedDeg = 90.0f;
	float maxLeanAngleDeg = 30.0f;
	float leanSpeedDeg = 90.0f;
	float wheelBase = 1.6f;
	float driveForce = 1000.0f;
	float brakeForce = 20.0f;
	float maxSpeed = 60.0f;
	float gravityFactor = 1.0f;
};

struct UserState
{
	float mouseX = 0.f;
	float mouseY = 0.f;

	float mouseLastX = 0.f;
	float mouseLastY = 0.f;


	//Spherical Coordinates
	float Yaw = 0.f;
	float Pitch = 0.f;
	float Distance = 0.f;

	bool previousMouseState = false; // true = right button held
	bool wasMousing = false; // true = camera active

	glm::mat4 camera2world = glm::identity<glm::mat4>();

	int renderMode = 0; // 0=Default, 1=Mip, 2=Depth, 3=Deriv

	bool mosaicEnabled = false; // key 5 toggle

	//================UI System================================
	bool particlesEnabled = true;//particle system toggle with key R

	bool isGameStarted = false;//game start toggle

	bool isGameOver = false;//game over toggle

	bool isGamePause = false;//game Pause toggle

	//UI 窗口的显示开关
	bool showControlPanel = true;
	bool showContentBrowser = true;
	bool showSceneHierarchy = true;
	bool showEntityInspector = true;
	bool showConsole = true;
	bool showLightPanel = true;
	bool showCameraPanel = true;
	bool showDebugPanel = true;

	bool debugSelectionBounds = false;
	bool debugCollisionShapes = true;

	float bikeSpeed = 0.0f;
	float bikeSteerAngle = 0.0f;
	BikeTuning bikeTuning{};

	// 记录当前选中的粒子索引 (-1 表示没选中任何粒子)
	int activeParticleIndex = -1;

	bool isSceneViewportHovered = false;
	//================UI System================================

	glm::vec3 followTargetPos = glm::vec3(10.f);
	bool thirdPersonMode = true;

	bool bloomEnabled = true;


	float cameraFov = 85.0f;
};