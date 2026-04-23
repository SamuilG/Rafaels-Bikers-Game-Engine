#include "Application.hpp"
#include "../Renderer/RenderSystem.hpp"
#include "../Scene/SceneManager.hpp"
#include "../Physics/PhysicsSystem.hpp"
#include "../Input/InputSystem.hpp"
#include "../Event/EventSystem.hpp"
#include "../Scene/TestScene.hpp" // 引入你的测试关卡

namespace engine {

    Application::Application() {
        inputSystem = AddSystem<InputSystem>();
        eventSystem = AddSystem<EventSystem>();

        physicsSystem = AddSystem<PhysicsSystem>();
        physicsSystem->SetEventSystem(eventSystem);
        physicsSystem->SetUserState(&mState);
        
        sceneManager = AddSystem<SceneManager>(physicsSystem);
        sceneManager->SetUserState(&mState);

        renderSystem = AddSystem<RenderSystem>(Running, sceneManager);
        renderSystem->SetUserState(&mState);

        for (auto& sys : Systems) {
            sys->Init();
        }

        if (inputSystem && renderSystem) {
            inputSystem->SetWindow(renderSystem->GetGLFWWindow());
            renderSystem->SetInputSystem(inputSystem);
            physicsSystem->SetInputSystem(inputSystem);
        }

        // 注册全局事件监听 (比如碰撞)
        eventSystem->Subscribe(EventType::Collision, [this](Event& e) {
            auto& collisionE = static_cast<CollisionEvent&>(e);
            // ... 你的碰撞日志打印逻辑 ...
        });

        // ==============================================================
        // 【核心】：加载当前关卡 (未来切换关卡，只需要 new 不同的 Scene 即可)
        // ==============================================================
        m_currentScene = std::make_unique<TestScene>();
        m_currentScene->Init(renderSystem, sceneManager, physicsSystem, inputSystem, eventSystem, &mState);
    }

    Application::~Application() {
        // 先销毁关卡
        if (m_currentScene) {
            m_currentScene->Shutdown();
            m_currentScene.reset();
        }

        for (auto it = Systems.rbegin(); it != Systems.rend(); ++it)
            (*it)->Shutdown();
        Systems.clear();
    }

    void Application::Run() {
        mLastTime = std::chrono::steady_clock::now();
        constexpr float kMaxDt = 0.05f; 

        while (Running) {
            float dt = std::min(CalcDeltaTime(), kMaxDt);

            // 更新当前关卡
            if (m_currentScene) {
                m_currentScene->Update(dt);
            }

            // 更新底层引擎系统
            for (auto& sys : Systems) {
                sys->Update(dt);
            }
        }
    }
}