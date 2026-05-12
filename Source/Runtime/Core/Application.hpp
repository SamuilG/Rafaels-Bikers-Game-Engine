#pragma once
#include <vector>
#include <memory>
#include <chrono>
#include "System.h"
#include "../Scene/GameScene.hpp"

#include "../UserState/UserState.hpp"

namespace engine {
    class BikeController;
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
        explicit Application(ProgressCallback progress = {});
        void ShowMainWindow();

        Application();
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
        void ReloadCurrentScene(bool returnToMainMenu);

        float CalcDeltaTime() {
            auto now = std::chrono::steady_clock::now();
            float dt = std::chrono::duration<float>(now - mLastTime).count();
            mLastTime = now;
            return dt;
        }

        std::vector<std::unique_ptr<System>> Systems;
        bool Running = true;

        ProgressCallback mProgressCallback;

        std::chrono::steady_clock::time_point mLastTime
            = std::chrono::steady_clock::now();

        // Stored for post-Init sphere setup
        std::unique_ptr<BikeController> m_bikeController;
        PhysicsSystem* physicsSystem = nullptr;
        SceneManager* sceneManager = nullptr;
        RenderSystem* renderSystem = nullptr;
        InputSystem* inputSystem = nullptr;
        EventSystem* eventSystem = nullptr;
        UserState mState;
        AnimationSystem* animationSystem = nullptr;
        std::unique_ptr<GameScene> m_currentScene;

        AudioSystem* audioSystem = nullptr;
    };

} // namespace engine
