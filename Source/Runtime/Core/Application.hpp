#pragma once
#include <vector>
#include <memory>
#include <chrono>
#include "System.h"

namespace engine {
    class PhysicsSystem;
    class SceneManager;
    class RenderSystem;
    class InputSystem;

    class Application {
    public:
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
        float CalcDeltaTime() {
            auto now = std::chrono::steady_clock::now();
            float dt = std::chrono::duration<float>(now - mLastTime).count();
            mLastTime = now;
            return dt;
        }

        std::vector<std::unique_ptr<System>> Systems;
        bool Running = true;

        std::chrono::steady_clock::time_point mLastTime
            = std::chrono::steady_clock::now();

        // Stored for post-Init sphere setup
        PhysicsSystem* physicsSystem = nullptr;
        SceneManager*  sceneManager  = nullptr;
        RenderSystem*  renderSystem  = nullptr;
        InputSystem*   inputSystem   = nullptr;
    };

} // namespace engine