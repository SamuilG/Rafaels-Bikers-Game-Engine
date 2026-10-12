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

inline int ActiveRenderMode(const EditorState& editor, bool renderingGame) {
    return editor.showEngineUi && !renderingGame
        ? std::clamp(editor.renderMode, int(view_mode::Default), int(view_mode::Count) - 1)
        : view_mode::Default;
}

} // namespace engine::editor_viewport
