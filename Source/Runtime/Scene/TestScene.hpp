#pragma once
#include "GameScene.hpp"
#include "../Physics/bikeController.hpp"
#include "../Animation/AnimationTypes.hpp"
#include <memory>
#include <vector>
#include <flecs.h>
#include <glm/glm.hpp>

namespace engine {

    class TestScene : public GameScene {
    public:
        TestScene();
        ~TestScene() override;

        void Init(RenderSystem* render, SceneManager* scene, PhysicsSystem* physics, InputSystem* input, EventSystem* eventSys, AnimationSystem* animation, UserState* state) override;
        void Update(float dt) override;
        void Shutdown() override;

    private:
        RenderSystem* m_render = nullptr;
        SceneManager* m_scene = nullptr;
        PhysicsSystem* m_physics = nullptr;
        InputSystem* m_input = nullptr;
        EventSystem* m_event = nullptr;
        AnimationSystem* m_animation = nullptr;
        UserState* m_state = nullptr;

        std::unique_ptr<BikeController> m_bikeController;
        std::vector<flecs::entity> m_playerEntities;
        flecs::entity m_cameraTarget = flecs::entity::null();
        glm::vec3 m_playerPosition = glm::vec3(30.0f, 0.0f, 30.0f);
        float m_playerYaw = 0.0f;
        float m_playerScale = 0.02f;
        size_t m_playerAnimatorIndex = 0;
        AnimationClip m_idleClip{};
        AnimationClip m_walkClip{};
        AnimationClip m_runClip{};
        uint32_t m_playerSkeletonIndex = 0;
        uint32_t m_playerPoseIndex = 0;

        void SwitchToClip(const AnimationClip& clip);
    };

} // namespace engine
