#pragma once
#include <vector>
#include <memory>
#include <chrono>
#include <string_view>
#include "System.h"
#include "ApplicationOptions.hpp"
#include "EditorPlayController.hpp"
#include "../Scene/GameScene.hpp"

#include "../UserState/UserState.hpp"

namespace engine {
    class PhysicsSystem;
    class SceneManager;
    class RenderSystem;
    class InputSystem;
    class EventSystem;
    class AnimationSystem;
    class AudioSystem;

    class Application {
    public:
        using ProgressCallback = std::function<void(float, std::string_view)>;
        explicit Application(ApplicationOptions options, ProgressCallback progress = {});
        void ShowMainWindow();

        ~Application();
        void Run();

        template<typename T, typename... Args>
        T* AddSystem(Args&&... args) {
            auto& ref = Systems.emplace_back(
                std::make_unique<T>(std::forward<Args>(args)...));
            return static_cast<T*>(ref.get());
        }


    private:
        void ReportProgress(float progress, std::string_view stage) const;
        bool ReloadCurrentScene();
        std::unique_ptr<GameScene> CreateScene() const;
        void ApplySceneWorkspace();
        std::string StartEditorPlay();
        void StopEditorPlay();
        void ApplyEditorSceneToPlay();
        void ResetRuntimeUi();
        struct EditorPlaySession;

        float CalcDeltaTime() {
            auto now = std::chrono::steady_clock::now();
            float dt = std::chrono::duration<float>(now - mLastTime).count();
            mLastTime = now;
            return dt;
        }

        ApplicationOptions mOptions;
        std::vector<std::unique_ptr<System>> Systems;
        bool Running = true;

        ProgressCallback mProgressCallback;

        std::chrono::steady_clock::time_point mLastTime
            = std::chrono::steady_clock::now();

        PhysicsSystem* physicsSystem = nullptr;
        SceneManager* sceneManager = nullptr;
        RenderSystem* renderSystem = nullptr;
        InputSystem* inputSystem = nullptr;
        EventSystem* eventSystem = nullptr;
        UserState mState;
        GameplayState mGameplayState = mState.Gameplay();
        SceneStateView mSceneState = mState.Scene();
        RendererStateView mRendererState = mState.Renderer();
        AnimationSystem* animationSystem = nullptr;
        std::unique_ptr<GameScene> m_currentScene;

        AudioSystem* audioSystem = nullptr;
        EditorPlayController mEditorPlay;
        std::unique_ptr<EditorPlaySession> mEditorPlaySession;
    };

} // namespace engine
