
#pragma once
#include "GameplayState.hpp"

namespace engine {

	struct UserState : public GameplayState
	{
		int renderMode = 0; // 0=Default, 1=Mip, 2=Depth, 3=Deriv

		//================UI System================================

		bool showEngineUi = false;//engine UI toggle with key F1

		bool particlesEnabled = true;//particle system toggle with key R

		//UI 窗口的显示开关
		bool showControlPanel = true;
		bool showContentBrowser = true;
		bool showSceneHierarchy = true;
		bool showEntityInspector = true;
		bool showConsole = true;
		bool showLightPanel = true;
		bool showCameraPanel = true;
		bool showDebugPanel = true;
		bool showAudioPanel = true;
		bool showGameUiEditor = false;

		bool debugSelectionBounds = false;
		bool debugCollisionShapes = true;
		bool frustumCullingEnabled = false; //frustum culling

		float frustumCullingOffFps = 0.0f; //off frustum culling fps
		float frustumCullingOnFps = 0.0f; // On frustum culling fps
		uint32_t frustumCullingTotalCandidates = 0; // new frustum culling
		uint32_t frustumCullingVisibleCandidates = 0; // new frustum culling
		float frustumCullingPadding = 0.5f; // new frustum culling

		bool  lodEnabled = true;    // distance-based LOD selection
		float lodDebugDistance = -1.0f;   // -1 = inactive; positive value overrides distance for testing

		//================UI System================================


		// 记录当前选中的粒子索引 (-1 表示没选中任何粒子)
		int activeParticleIndex = -1;

		bool isSceneViewportHovered = false;


		//================Graphic================================
		bool mosaicEnabled = false; // key 5 toggle

		// ----- 后处理（可在 UI 实时调节） -----
		float bloomExposure = 1.0f;      // 合成阶段曝光（传给 composite shader）
		float bloomStrength = 1.2f;      // Bloom 强度倍数（传给 composite shader）
		float bloomThreshold = 1.0f;     // 亮度提取阈值（preview / reserved）
		int   bloomKernelRadius = 7;     // 模糊核半径提示（仅作为 shader 调整参考）
		bool  bloomUseACES = true;       // 是否使用 ACES 风格色调映射（reserved）
		//================Graphic================================
	};
} // namespace engine
