#include "Application.hpp"
#include "../Renderer/RenderSystem.hpp"
#include "../Scene/SceneManager.hpp"
#include "../Physics/PhysicsSystem.hpp"
#include "../Input/InputSystem.hpp"
#include "../Event/EventSystem.hpp"
#include "../Animation/AnimationSystem.hpp"
#include "../Scene/TestScene.hpp" // 引入你的测试关卡
#include "../AudioSystem/AudioSystem.hpp"
#include "tracy/Tracy.hpp"


namespace engine {

    Application::Application() {
        inputSystem = AddSystem<InputSystem>();
        eventSystem = AddSystem<EventSystem>();
		audioSystem = AddSystem<AudioSystem>();//音频系统

        physicsSystem = AddSystem<PhysicsSystem>();
        physicsSystem->SetEventSystem(eventSystem);
        physicsSystem->SetUserState(&mState);

        sceneManager = AddSystem<SceneManager>(physicsSystem);
        sceneManager->SetUserState(&mState);

        animationSystem = AddSystem<AnimationSystem>();
        animationSystem->set_scene_manager(sceneManager);

        renderSystem = AddSystem<RenderSystem>(Running, sceneManager);
        renderSystem->SetUserState(&mState);
        renderSystem->set_animation_system(animationSystem);
		renderSystem->SetAudioSystem(audioSystem);
        
		

        for (auto& sys : Systems) {
            sys->Init();
        }

        if (inputSystem && renderSystem) {
            inputSystem->SetWindow(renderSystem->GetGLFWWindow());
            renderSystem->SetInputSystem(inputSystem);
            physicsSystem->SetInputSystem(inputSystem);
        }

        // audio system test
        if (audioSystem) {
            //background music
            audioSystem->LoadSound("BackgroundTestMusic", "Assets/Sounds/BackgroundTestMusic.mp3");
            audioSystem->SetVolume("BackgroundTestMusic", 0.1f);
            audioSystem->SetPitch("BackgroundTestMusic", 1.0f);
            audioSystem->PlayLoop("BackgroundTestMusic");
            //bike chain sound effect
            audioSystem->LoadSound("BikeChain", "Assets/Sounds/BikeChain.mp3");
            audioSystem->SetVolume("BikeChain", 0.6f);
            audioSystem->SetRuntimeVolume("BikeChain", 0.0f);
            audioSystem->SetPitch("BikeChain", 1.0f);
            audioSystem->PlayLoop("BikeChain");
            audioSystem->LoadSound("Chain", "Assets/Sounds/BikeChain.mp3");
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
        m_currentScene->Init(renderSystem, sceneManager, physicsSystem, inputSystem, eventSystem, &mState, animationSystem, audioSystem);
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

            //audio system
           // 根据自行车速度状态调整音效// Adjust bike chain sound based on bike speed
            if (audioSystem) {
                float speed01 = std::clamp(mState.bikeSpeed / 40.0f, 0.0f, 1.0f);

                audioSystem->SetRuntimeVolume("BikeChain", speed01);
                audioSystem->SetPitch("BikeChain", 0.75f + speed01 * 1.25f);
            }
            FrameMark;
        }
    }
}