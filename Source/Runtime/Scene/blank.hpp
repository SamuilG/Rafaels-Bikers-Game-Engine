#pragma once

#include "GameScene.hpp"

namespace engine {

// Generic engine entry scene for new and document-based projects. It owns no
// hard-coded game assets; the host instantiates the authored scene document.
class blank final : public GameScene {
public:
    blank() = default;
    ~blank() override = default;

    void Init(RenderSystem* render, SceneManager* scene, PhysicsSystem* physics,
        InputSystem* input, EventSystem* eventSys, GameplayState* state,
        AnimationSystem* anima, AudioSystem* audio) override;
    void Update(float dt) override;
    void RefreshPlayerMotion(float dt) override;
    void Shutdown() override;

private:
    RenderSystem* m_render = nullptr;
    SceneManager* m_scene = nullptr;
    PhysicsSystem* m_physics = nullptr;
    InputSystem* m_input = nullptr;
    EventSystem* m_event = nullptr;
    GameplayState* m_state = nullptr;
    AnimationSystem* m_animation = nullptr;
    AudioSystem* m_audio = nullptr;
};

} // namespace engine
