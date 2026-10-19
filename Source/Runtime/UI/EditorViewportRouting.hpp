#pragma once

#include "../UserState/EditorState.hpp"
#include "../Renderer/RenderUtilities/ViewMode.hpp"
#include <algorithm>

namespace engine::editor_viewport {

// Scene and Game share one render target. Selecting a Scene diagnostic must
// also select its tab, otherwise the Game route deliberately forces Default.
inline void SelectSceneViewMode(EditorState& editor, int mode) {
    editor.renderMode = std::clamp(mode, int(view_mode::Default), int(view_mode::Count) - 1);
    editor.focusGameViewport = false;
    editor.focusSceneViewport = true;
}

inline bool RenderGameViewport(const EditorState& editor) {
    return !editor.showEngineUi ||
        (editor.isGameViewportVisible && !editor.isSceneViewportVisible);
}

inline bool FreeCameraPreview(const EditorState& editor, bool playing, CameraMode mode) {
    return editor.showEngineUi && !playing && mode == CameraMode::Free;
}

inline bool FreeCameraPreviewAcceptsInput(const EditorState& editor, bool playing,
    CameraMode mode, bool pointerInside, bool mouseCaptured) {
    return FreeCameraPreview(editor, playing, mode) && editor.isGameViewportVisible &&
        editor.isGameViewportFocused && !editor.inputCapturesKeyboard && (pointerInside || mouseCaptured);
}

inline int ActiveRenderMode(const EditorState& editor, bool renderingGame) {
    return editor.showEngineUi && !renderingGame
        ? std::clamp(editor.renderMode, int(view_mode::Default), int(view_mode::Count) - 1)
        : view_mode::Default;
}

inline bool PortalsEnabled(const EditorState& editor, bool renderingGame, bool configured) {
    // A hidden Scene diagnostic cannot remove portal surfaces/passes from Game.
    return configured && ActiveRenderMode(editor, renderingGame) == view_mode::Default;
}

} // namespace engine::editor_viewport
