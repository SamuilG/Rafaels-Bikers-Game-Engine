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
namespace engine {
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

		bool isGameStarted = true;//game start toggle

		bool isGameOver = false;//game over toggle

		bool isGamePause = false;//game Pause toggle

		//UI 窗口的显示开�?
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
		bool frustumCullingEnabled = false; //frustum culling

		float frustumCullingOffFps = 0.0f; //off frustum culling fps
		float frustumCullingOnFps = 0.0f; // On frustum culling fps
		uint32_t frustumCullingTotalCandidates = 0; // new frustum culling
		uint32_t frustumCullingVisibleCandidates = 0; // new frustum culling
		float frustumCullingPadding = 0.5f; // new frustum culling

		bool  lodEnabled       = true;    // distance-based LOD selection
		float lodDebugDistance = -1.0f;   // -1 = inactive; positive value overrides distance for testing

		float bikeSpeed = 0.0f;
		float bikeSteerAngle = 0.0f;
		//extreme speed state
		bool isExtremeSpeed = false;

		BikeTuning bikeTuning{};

		// 记录当前选中的粒子索�?(-1 表示没选中任何粒子)
		int activeParticleIndex = -1;

		bool isSceneViewportHovered = false;
		//================UI System================================

		glm::vec3 followTargetPos = glm::vec3(2.f);
		bool thirdPersonMode = true;

		bool bloomEnabled = true;


		float cameraFov = 85.0f;
		float targetFov = 85.0f; // 【新增】用于插值的 FOV 目标�?
		// ==========================================
		// lerp for camera free look
		float targetYaw = 0.f;
		float targetPitch = 0.f;
		float targetDistance = 5.0f;

		// ==========================================
		// 【新增】：相机自动回正所需变量
		float cameraIdleTimer = 0.0f; // 记录玩家多久没碰相机�?
		float bikeYaw = 0.0f;         // 记录单车当前的车头真实朝�?
		float playerSpeed = 0.0f;
		float playerYaw = 0.0f;
		
		float bikeLeanAngle = 0.0f;    // 接收来自物理系统的压弯角�?
		float cameraRoll = 0.0f;       // 相机当前�?Roll 角度
		float targetCameraRoll = 0.0f; // 相机目标�?Roll 角度
		// ==========================================
	};
} // namespace engine
