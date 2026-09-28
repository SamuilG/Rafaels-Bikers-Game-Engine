#include "BikersProject.hpp"

#include "Runtime/Project/ProjectDescriptor.hpp"
#include "Runtime/Scene/level1.hpp"
#include "Runtime/Renderer/RenderSystem.hpp"
#include "Runtime/AudioSystem/AudioSystem.hpp"
#include "Runtime/UI/GameUi.hpp"
#include <algorithm>
#include <filesystem>
#include <stdexcept>

namespace engine::bikers {

ApplicationOptions CreateOptions(const ProjectDescriptor& project, bool editor) {
    if (project.contentModule != "bikers")
        throw std::runtime_error("Bikers project cannot load content module: " + project.contentModule);
    if (project.startupScene != "level1")
        throw std::runtime_error("Bikers project does not provide startup scene: " + project.startupScene);

    // Check core content before constructing Vulkan/physics systems.
    for (const auto* relative : {
        "Models/Level.glb", "Models/tbikeWithAnchor.glb", "Models/character.glb",
        "Models/respawnPoint.glb", "ui/MainMenu.ui.json", "ui/HUD.ui.json",
        "ui/AbilityUnlock.ui.json", "ui/PauseMenu.ui.json", "ui/Settings.ui.json",
        "ui/Win.ui.json", "ui/RespawnPrompt.ui.json", "Sounds/BikeChain.mp3" }) {
        const auto path = project.assetRoot / relative;
        if (!std::filesystem::is_regular_file(path))
            throw std::runtime_error("Bikers project asset is missing: " + path.string());
    }

    ApplicationOptions options;
    options.createScene = [] { return std::make_unique<level>(); };
    options.windowTitle = editor ? "Steer Engine - " + project.name : project.name + " - Demo";
    options.imguiIniPath = (project.filePath.parent_path() / ".steer" /
        (editor ? "Editor.ini" : "Demo.ini")).string();
    options.editorAvailable = editor;
    options.showEditorInitially = editor;
    options.runtimeUiEnabled = true;
    options.startPlaying = false;
    options.editingProject = editor;
    options.playInEditorAvailable = editor;
    options.sceneSnapshotPath = project.snapshotPath.string();
    options.editorCameraPosition = glm::vec3(-145.0f, 10.0f, -72.0f);
    options.editorCameraTarget = glm::vec3(-152.0f, 3.0f, -84.0f);
    options.configureRenderer = [](RenderSystem& renderer) {
        renderer.SetGameHudCallbacks(
            [&renderer](const HudData& hud, const ImVec2& position, const ImVec2& size) {
                GameUi::DrawHud(hud, position, size,
                    [&renderer](const char* path) {
                        return (ImTextureID)renderer.GetContentBrowserThumbnail(path);
                    });
            }, [] { GameUi::ResetTransientState(); });
    };
    options.preloadRuntimeUi = [](RuntimeUiController& ui) {
        for (const auto* path : {
            "Assets/ui/MainMenu.ui.json", "Assets/ui/HUD.ui.json",
            "Assets/ui/AbilityUnlock.ui.json", "Assets/ui/PauseMenu.ui.json",
            "Assets/ui/Settings.ui.json", "Assets/ui/Win.ui.json" }) {
            if (!ui.PreloadWidget(path))
                throw std::runtime_error(std::string("Cannot load Bikers UI: ") + path);
        }
    };
    options.initializeAudio = [](AudioSystem& audio) {
        audio.LoadSound("BikeChain", "Assets/Sounds/BikeChain.mp3");
        audio.SetVolume("BikeChain", 0.2f);
        audio.SetRuntimeVolume("BikeChain", 0.0f);
        audio.SetPitch("BikeChain", 1.0f);
        audio.PlayLoop("BikeChain");
        audio.LoadSound("Chain", "Assets/Sounds/BikeChain.mp3");
    };
    options.updateAudio = [](AudioSystem& audio, const PlayerState& player,
        const FrameExecution& frame) {
        const float speed = frame.runSimulationSystems
            ? std::clamp(player.bikeSpeed / 40.0f, 0.0f, 1.0f) : 0.0f;
        audio.SetRuntimeVolume("BikeChain", speed);
        audio.SetPitch("BikeChain", 0.75f + speed * 1.25f);
    };
    return options;
}

}
