#pragma once

namespace engine {
struct EditorState {
    int renderMode = 0; // Stable IDs are defined in Renderer/RenderUtilities/ViewMode.hpp.
    bool editorViewportBackdrop = true; // Neutral editor background; disable to preview scene sky.
    bool editorViewportGrid = true;     // Depth-tested reference grid on the XZ plane.
    bool showEngineUi = false;//engine UI toggle with key F1
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
    // Published after the ImGui frame and consumed by the next simulation
    // phase. This keeps editor text entry from reaching gameplay controls.
    bool inputCapturesKeyboard = false;
    bool inputCapturesMouse = false;
    bool debugSelectionBounds = false;
    bool debugCollisionShapes = true;
    float lodDebugDistance = -1.0f;   // -1 = inactive; positive value overrides distance for testing
    int activeParticleIndex = -1;
    bool isSceneViewportHovered = false;
};
}
