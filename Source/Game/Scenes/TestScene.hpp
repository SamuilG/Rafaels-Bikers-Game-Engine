#pragma once
#include "../../Runtime/Scene/GameScene.hpp"
#include "../Systems/bikeController.hpp"
#include <memory>

namespace engine {

    class TestScene : public GameScene {
    public:
        TestScene();
        ~TestScene() override;

        void Init(RenderSystem* render, SceneManager* scene, PhysicsSystem* physics, InputSystem* input, EventSystem* eventSys, UserState* state, AnimationSystem* anima, AudioSystem* audio) override;
        void Update(float dt) override;
        void Shutdown() override;

    private:

        RenderSystem* m_render = nullptr;
        SceneManager* m_scene = nullptr;
        PhysicsSystem* m_physics = nullptr;
        InputSystem* m_input = nullptr;
        EventSystem* m_event = nullptr;
        UserState* mState = nullptr;
		AnimationSystem* m_anima = nullptr;
        AudioSystem* m_audio = nullptr;


        std::unique_ptr<BikeController> m_bikeController;

        //sound delay
        float m_allCollectSoundDelay = -1.0f;
    };

} // namespace engine
