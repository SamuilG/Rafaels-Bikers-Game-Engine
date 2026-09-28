#include "Application.hpp"
#include "../Renderer/RenderSystem.hpp"
#include "../Scene/SceneManager.hpp"
#include "../Physics/PhysicsSystem.hpp"
#include "../Input/InputSystem.hpp"
#include "../Event/EventSystem.hpp"
#include "../Animation/AnimationSystem.hpp"
#include "../Scene/level1.hpp"
#include "../AudioSystem/AudioSystem.hpp"
#include "../UI/VisualUIEditor/RuntimeUiController.hpp"
#include "FrameScheduler.hpp"

namespace engine {

    namespace {
        // Startup and restart must choose the same playable demo. The empty
        // engine scene remains available for explicit development use.
        std::unique_ptr<GameScene> CreateDefaultScene() {
            return std::make_unique<level>();
        }
    }

    Application::Application(ProgressCallback progress) : mProgressCallback(std::move(progress))
    {
        ReportProgress(0.02f, "Creating engine systems...");

        inputSystem = AddSystem<InputSystem>();
        eventSystem = AddSystem<EventSystem>();
		audioSystem = AddSystem<AudioSystem>();//音频系统

        physicsSystem = AddSystem<PhysicsSystem>();
        physicsSystem->SetEventSystem(eventSystem);


        sceneManager = AddSystem<SceneManager>(physicsSystem);
        sceneManager->SetState(&mSceneState);

        animationSystem = AddSystem<AnimationSystem>();
        animationSystem->set_scene_manager(sceneManager);

        renderSystem = AddSystem<RenderSystem>(Running, sceneManager);
        renderSystem->SetState(&mRendererState);
        renderSystem->set_animation_system(animationSystem);
		renderSystem->SetAudioSystem(audioSystem);
        
        //splash
        constexpr float kRenderInitStart = 0.10f;
        constexpr float kRenderInitEnd = 0.68f;
        renderSystem->SetInitProgressCallback([this](float progressValue, std::string_view stage) {
            float clamped = std::clamp(progressValue, 0.0f, 1.0f);
            ReportProgress(kRenderInitStart + (kRenderInitEnd - kRenderInitStart) * clamped, stage);
            });

        ReportProgress(0.10f, "Initializing subsystems...");
        
        for (auto& sys : Systems) {
            sys->Init();
        }

        renderSystem->SetInitProgressCallback({});

        ReportProgress(0.68f, "Connecting engine services...");
        if (inputSystem && renderSystem) {
            inputSystem->SetWindow(renderSystem->GetGLFWWindow());
            renderSystem->SetInputSystem(inputSystem);

        }

		// UI 系统初始化
        ReportProgress(0.7f, "Loading startup audio...");
        RuntimeUiController* runtimeUiController = renderSystem ? renderSystem->GetRuntimeUiController() : nullptr;
        if (runtimeUiController) {
            runtimeUiController->PreloadWidget("Assets/ui/MainMenu.ui.json");
            runtimeUiController->PreloadWidget("Assets/ui/HUD.ui.json");
            runtimeUiController->PreloadWidget("Assets/ui/AbilityUnlock.ui.json");
            runtimeUiController->PreloadWidget("Assets/ui/PauseMenu.ui.json");
            runtimeUiController->PreloadWidget("Assets/ui/Settings.ui.json");
            runtimeUiController->PreloadWidget("Assets/ui/Win.ui.json");
        }

        m_currentScene = CreateDefaultScene();

		//audio system 初始化
        ReportProgress(0.74f, "Loading startup audio...");
        // audio system test
        if (audioSystem && m_currentScene->UsesVehicleAudio()) {
            // Background music is now started by the Radio pickup in level1.
            // audioSystem->LoadSound("BackgroundTestMusic", "Assets/Sounds/Looping_radio_mix.mp3");
            // audioSystem->SetVolume("BackgroundTestMusic", 0.1f);
            // audioSystem->SetPitch("BackgroundTestMusic", 1.0f);
            // audioSystem->PlayLoop("BackgroundTestMusic");

            //bike chain sound effect
            audioSystem->LoadSound("BikeChain", "Assets/Sounds/BikeChain.mp3");
            audioSystem->SetVolume("BikeChain", 0.2f);
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


        ReportProgress(0.82f, "Loading level...");
        // ==============================================================
        // 【核心】：加载当前关卡 (未来切换关卡，只需要 new 不同的 Scene 即可)
        // ==============================================================
        m_currentScene->Init(renderSystem, sceneManager, physicsSystem, inputSystem, eventSystem, &mGameplayState, animationSystem, audioSystem);
        sceneManager->Update(0.0f);
        animationSystem->Update(0.0f);
        if (runtimeUiController) runtimeUiController->SyncGameFlowUi();
        ReportProgress(1.0f, "Ready");
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
        const FrameSteps steps{
            .gameplay = [this](float dt) { if (m_currentScene) m_currentScene->Update(dt); },
            .events = [this](float dt) { eventSystem->Update(dt); },
            .physics = [this](float dt) { physicsSystem->Update(dt); },
            .postPhysics = [this](float dt) {
                if (m_currentScene) m_currentScene->RefreshPlayerMotion(dt);
                mState.player.UpdateEffects(dt);
                if (m_currentScene) m_currentScene->UpdateTriggers(mState.player.State().position);
            },
            .scene = [this](float dt) { sceneManager->Update(dt); },
            .animation = [this](float dt) { animationSystem->Update(dt); },
            .gameEffects = [this](float dt) { if (m_currentScene) m_currentScene->UpdatePresentation(dt); },
            .presentation = [this](float dt) { renderSystem->Update(dt); },
            .audio = [this](float dt) {
                audioSystem->Update(dt);
                if (m_currentScene && m_currentScene->UsesVehicleAudio()) {
                    const float speed01 = mState.frame.runSimulationSystems
                        ? std::clamp(mState.player.State().bikeSpeed / 40.0f, 0.0f, 1.0f) : 0.0f;
                    audioSystem->SetRuntimeVolume("BikeChain", speed01);
                    audioSystem->SetPitch("BikeChain", 0.75f + speed01 * 1.25f);
                }
            }
        };

        while (Running) {
            const float dt = std::min(CalcDeltaTime(), kMaxDt);

            // Sample once, then publish the previous frame's accepted commands.
            inputSystem->Update(dt);
            mState.frame = FrameScheduler::BeginFrame(mState.gameFlow, [this]() {
                try {
                    return ReloadCurrentScene();
                }
                catch (const std::exception& error) {
                    EngineUi::LogPrintf("Engine reload/recovery failed: %s\n", error.what());
                    Running = false;
                    return false;
                }
            });
            if (!Running) break;

            if (mState.frame.reloadHandled) {
                inputSystem->ResetForNewSession();
                mLastTime = std::chrono::steady_clock::now();
            }
            if (auto* ui = renderSystem->GetRuntimeUiController()) ui->SyncGameFlowUi();

            const bool editorOwnsKeyboard = mState.editor.showEngineUi &&
                (mState.editor.inputCapturesKeyboard || !mState.editor.isSceneViewportHovered);
            inputSystem->SetGameplayInputEnabled(mState.frame.runGameplay && !editorOwnsKeyboard);

            // Every consumer sees this same snapshot until the next boundary.
            // UI/gameplay requests made by these steps cannot change its gates.
            FrameScheduler::Execute(mState.frame, dt, steps);
        }
    }

    void Application::ReportProgress(float progress, std::string_view stage) const {
        if (mProgressCallback) {
            mProgressCallback(progress, stage);
        }
    }

    void Application::ShowMainWindow() {
        if (renderSystem) {
            renderSystem->ShowMainWindow();
        }
    }

    bool Application::ReloadCurrentScene() {
        // Initialization propagates transforms without advancing a simulation frame.
        mState.frame = FrameExecution{};
        renderSystem->WaitForGpuIdle();
        const float preserveMasterVolume = audioSystem ? audioSystem->GetMasterVolume() : 1.0f;
        const bool useVehicleAudio = m_currentScene && m_currentScene->UsesVehicleAudio();

        if (m_currentScene) {
            m_currentScene->Shutdown();
            m_currentScene.reset();
        }

        if (renderSystem) {
            renderSystem->ClearSceneTransientResources();
        }

        if (inputSystem) {
            inputSystem->SetGameplayInputEnabled(false);
            inputSystem->ResetForNewSession();
        }

        if (eventSystem) {
            eventSystem->Shutdown();
            eventSystem->Init();
        }

        if (audioSystem) {
            audioSystem->Shutdown();
            audioSystem->Init();
            audioSystem->SetMasterVolume(preserveMasterVolume);
            if (useVehicleAudio) {
                audioSystem->LoadSound("BikeChain", "Assets/Sounds/BikeChain.mp3");
                audioSystem->SetVolume("BikeChain", 0.2f);
                audioSystem->SetRuntimeVolume("BikeChain", 0.0f);
                audioSystem->SetPitch("BikeChain", 1.0f);
                audioSystem->PlayLoop("BikeChain");
                audioSystem->LoadSound("Chain", "Assets/Sounds/BikeChain.mp3");
            }
        }

        if (physicsSystem) {
            physicsSystem->Shutdown();
            physicsSystem->Init();
            physicsSystem->SetEventSystem(eventSystem);

        }

        if (sceneManager) {
            sceneManager->Shutdown();
            sceneManager->Init();
        }

        if (animationSystem) {
            animationSystem->Shutdown();
            animationSystem->Init();
            animationSystem->set_scene_manager(sceneManager);
        }

        mState.ResetSession();

        if (sceneManager) {
            sceneManager->SetState(&mSceneState);
        }
        if (renderSystem) {
            renderSystem->SetState(&mRendererState);
        }

        // Only level loading failures can recover to a menu. Rebuilding an engine
        // subsystem above must finish before another frame is allowed to render.
        try {
            m_currentScene = CreateDefaultScene();
            m_currentScene->Init(renderSystem, sceneManager, physicsSystem, inputSystem, eventSystem, &mGameplayState, animationSystem, audioSystem);
            sceneManager->Update(0.0f);
            animationSystem->Update(0.0f);
            return true;
        }
        catch (const std::exception& error) {
            EngineUi::LogPrintf("Level load failed; returning to retry menu: %s\n", error.what());
            renderSystem->WaitForGpuIdle();
            if (m_currentScene) {
                m_currentScene->Shutdown();
                m_currentScene.reset();
            }
            renderSystem->ClearSceneTransientResources();
            eventSystem->Shutdown();
            eventSystem->Init();
            animationSystem->Shutdown();
            animationSystem->Init();
            animationSystem->set_scene_manager(sceneManager);
            sceneManager->Shutdown();
            sceneManager->Init();
            return false;
        }
    }
}
