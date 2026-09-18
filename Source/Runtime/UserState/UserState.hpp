
#pragma once
#include "GameplayState.hpp"

namespace engine {

	struct UserState : public GameplayState
	{
		int renderMode = 0; // Stable IDs are defined in Renderer/RenderUtilities/ViewMode.hpp.
		bool wireframeSupported = false; // Set from the active Vulkan device; read-only in the UI.

		//================UI System================================

		bool showEngineUi = false;//engine UI toggle with key F1
		bool editorViewportBackdrop = true; // Neutral editor background; disable to preview scene sky.
		bool editorViewportGrid = true;     // Depth-tested reference grid on the XZ plane.

		bool particlesEnabled = true; // Shared simulation/rendering switch.

		//UI 窗口的显示开关
		bool showRenderSettings = true;
		bool showContentBrowser = true;
		bool showSceneHierarchy = true;
		bool showEntityInspector = true;
		bool showConsole = true;
		bool showLightPanel = false;
		bool showCameraPanel = false;
		bool showDebugPanel = false;
		bool showAudioPanel = false;
		bool showParticlePanel = false;
		bool showRuntimeUiDebugPanel = false;
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
		float bloomStrength = 2.2f;      // Preserve the renderer's original composite strength.
		//================Graphic================================
	};
} // namespace engine
