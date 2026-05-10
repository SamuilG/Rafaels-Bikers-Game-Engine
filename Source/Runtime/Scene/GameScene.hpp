#pragma once
#include <string>

namespace engine {

    class RenderSystem;
    class SceneManager;
    class PhysicsSystem;
    class InputSystem;
    class EventSystem;
    class AnimationSystem;
    class AudioSystem;
    struct GameplayState;

    class GameScene {
    public:
        virtual ~GameScene() = default;

        virtual void Init(RenderSystem* render, SceneManager* scene, PhysicsSystem* physics, InputSystem* input, EventSystem* eventSys, GameplayState* state, AnimationSystem* anima, AudioSystem* audio) = 0;

        virtual void Update(float dt) = 0;

        virtual void Shutdown() = 0;

    protected:
        // Logging & notification — forwards to EngineUi without exposing it to game code
        static void Log(const std::string& msg);
        static void Toast(const std::string& msg);

        // Runtime UI widget control — forwards to RuntimeUiController
        void InitBase(RenderSystem* render);
        bool AddWidget(const std::string& path);
        bool RemoveWidget(const std::string& path);

    private:
        RenderSystem* m_baseRender = nullptr;
    };

} // namespace engine
