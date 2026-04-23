#pragma once

namespace engine {

    class RenderSystem;
    class SceneManager;
    class PhysicsSystem;
    class InputSystem;
    class EventSystem;
    struct UserState;

    // 所有关卡的基类接口
    class GameScene {
    public:
        virtual ~GameScene() = default;

        // 传入引擎的核心系统指针，供关卡使用
        virtual void Init(RenderSystem* render, SceneManager* scene, PhysicsSystem* physics, InputSystem* input, EventSystem* eventSys, UserState* state) = 0;

        // 关卡每帧的更新逻辑
        virtual void Update(float dt) = 0;

        // 退出关卡时的清理工作
        virtual void Shutdown() = 0;
    };

} // namespace engine