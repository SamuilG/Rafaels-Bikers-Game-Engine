#pragma once
#include "GameScene.hpp"
#include "../Physics/bikeController.hpp"
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
        // 保存系统指针供 Update 使用
        RenderSystem* m_render = nullptr;
        SceneManager* m_scene = nullptr;
        PhysicsSystem* m_physics = nullptr;
        InputSystem* m_input = nullptr;
        EventSystem* m_event = nullptr;
        UserState* m_state = nullptr;
		AnimationSystem* m_anima = nullptr;
        AudioSystem* m_audio = nullptr;

        // 【关键】：把 BikeController 从 Application 移交到具体关卡中管理
        // 因为别的关卡（比如主菜单）可能根本不需要自行车
        std::unique_ptr<BikeController> m_bikeController;
    };

} // namespace engine