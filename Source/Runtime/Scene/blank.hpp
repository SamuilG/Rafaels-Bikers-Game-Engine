#pragma once

#include "GameScene.hpp"

namespace engine {

// Empty engine entry scene. It owns no game assets or gameplay entities; it
// only establishes a usable first-person editor/runtime camera.
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
