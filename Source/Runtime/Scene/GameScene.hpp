#pragma once
#include "../Trigger/trigger.hpp"
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
        virtual ~GameScene();

        virtual void Init(RenderSystem* render, SceneManager* scene, PhysicsSystem* physics, InputSystem* input, EventSystem* eventSys, GameplayState* state, AnimationSystem* anima, AudioSystem* audio) = 0;

        virtual void Update(float dt) = 0;

        // Resolve post-physics gameplay state and publish physical feedback before rendering.
        virtual void RefreshPlayerMotion(float dt) {}

        // Gameplay evaluation runs in the application simulation phase. Rendering
        // receives only const trigger geometry and particle visibility outputs.
        void UpdateTriggers(const glm::vec3& probePosition);
        virtual void UpdatePresentation(float dt) {}
        virtual void OnParticleGroupRemoved(size_t particleIndex) {}
        virtual bool IsParticleGroupSceneControlled(size_t particleIndex) const { return false; }

        // Game scenes may opt into game-specific services such as vehicle audio.
        // The blank engine entry leaves these services disabled.
        virtual bool UsesVehicleAudio() const { return true; }

        virtual void Shutdown() = 0;

    protected:
        TriggerSystem& Triggers() { return mTriggers; }
        const TriggerSystem& Triggers() const { return mTriggers; }

        // Logging & notification — forwards to EngineUi without exposing it to game code
        static void Log(const std::string& msg);
        static void Toast(const std::string& msg);

        // Runtime UI widget control — forwards to RuntimeUiController
        void InitBase(RenderSystem* render);
        bool AddWidget(const std::string& path);
        bool RemoveWidget(const std::string& path);

    private:
        void DetachRenderCallbacks();
        TriggerSystem mTriggers;
        RenderSystem* m_baseRender = nullptr;
    };

} // namespace engine
