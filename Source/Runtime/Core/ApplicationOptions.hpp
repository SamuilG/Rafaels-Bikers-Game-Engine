#pragma once

#include "../Scene/GameScene.hpp"
#include <functional>
#include <memory>
#include <string>
#include <glm/vec3.hpp>

namespace engine {
class AudioSystem;
class RenderSystem;
class RuntimeUiController;
struct PlayerState;
struct FrameExecution;

// Executable-owned composition. The runtime never chooses a concrete game
// scene or names its UI, audio or HUD assets.
struct ApplicationOptions {
    std::function<std::unique_ptr<GameScene>()> createScene;
    std::string windowTitle = "Steer Engine";
    std::string imguiIniPath;
    bool editorAvailable = false;
    bool showEditorInitially = false;
    bool runtimeUiEnabled = false;
    bool startPlaying = false;
    bool editingProject = false;
    bool playInEditorAvailable = false;
    std::string sceneSnapshotPath;
    glm::vec3 editorCameraPosition{0.0f, 2.0f, 5.0f};
    glm::vec3 editorCameraTarget{0.0f, 2.0f, 0.0f};

    std::function<void(RenderSystem&)> configureRenderer;
    std::function<void(RuntimeUiController&)> preloadRuntimeUi;
    std::function<void(AudioSystem&)> initializeAudio;
    std::function<void(AudioSystem&, const PlayerState&, const FrameExecution&)> updateAudio;
};
}
