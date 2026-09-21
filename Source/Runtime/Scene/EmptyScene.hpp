#pragma once

#include "GameScene.hpp"

namespace engine {

// Minimal engine-owned scene. It creates no game entities, loads no Bikers
// assets and keeps the renderer/editor available for a new project session.
class EmptyScene final : public GameScene {
public:
    void Init(RenderSystem* render, SceneManager*, PhysicsSystem*, InputSystem*,
        EventSystem*, GameplayState*, AnimationSystem*, AudioSystem*) override {
        InitBase(render);
    }

    void Update(float) override {}
    void Shutdown() override {}
};

} // namespace engine
