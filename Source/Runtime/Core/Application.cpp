#include "Application.hpp"
#include "../Renderer/RenderSystem.hpp"
#include "../Scene/SceneManager.hpp"
#include "../Physics/PhysicsSystem.hpp"
#include "../Input/InputSystem.hpp"
#include "../Event/EventSystem.hpp"
#include "../Animation/AnimationSystem.hpp"
#include "../../Game/Scenes/TestScene.hpp"
#include "../../Game/Scenes/Level1.hpp"
#include "../../Game/UI/GameFlowRenderer.hpp"
#include "../../Game/UI/GameHudRenderer.hpp"
#include "../AudioSystem/AudioSystem.hpp"
#include "../XR/OpenXrSystem.hpp"

#include <algorithm>
#include <print>

namespace engine {

    Application::Application(ProgressCallback progress, LaunchMode mode)
        : mLaunchMode(mode)
        , mProgressCallback(std::move(progress))
    {
        switch (mLaunchMode) {
        case LaunchMode::Desktop:
            InitializeDesktopMode();
            break;
        case LaunchMode::XR:
            InitializeXrMode();
            break;
        default:
            InitializeDesktopMode();
            break;
        }
    }

    void Application::CreateEngineSystems()
    {
        ReportProgress(0.02f, "Creating engine systems...");

        inputSystem = AddSystem<InputSystem>();
        eventSystem = AddSystem<EventSystem>();
        audioSystem = AddSystem<AudioSystem>();

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

        mGameFlowRenderer = std::make_unique<GameFlowRenderer>();
        mGameHudRenderer = std::make_unique<GameHudRenderer>();
        renderSystem->SetGameFlowRenderer(mGameFlowRenderer.get());
        renderSystem->SetGameHudRenderer(mGameHudRenderer.get());
    }

    void Application::ConfigureRenderProgressBridge()
    {
        constexpr float kRenderInitStart = 0.10f;
        constexpr float kRenderInitEnd = 0.68f;
        renderSystem->SetInitProgressCallback([this](float progressValue, std::string_view stage) {
            float clamped = std::clamp(progressValue, 0.0f, 1.0f);
            ReportProgress(kRenderInitStart + (kRenderInitEnd - kRenderInitStart) * clamped, stage);
        });
    }

    void Application::InitializeDesktopMode()
    {
        CreateEngineSystems();
        if (renderSystem) {
            renderSystem->SetVulkanCreateRequirements({});
        }
        ConfigureRenderProgressBridge();

        ReportProgress(0.10f, "Initializing desktop subsystems...");
        for (auto& sys : Systems) {
            sys->Init();
        }
        renderSystem->SetInitProgressCallback({});

        ConnectEngineServices();
        InitializeAudio();
        SubscribeGameplayEvents();
        LoadInitialScene();
        ReportProgress(1.0f, "Ready");
    }

    void Application::InitializeXrMode()
    {
        auto xrSystem = std::make_unique<OpenXrSystem>();
        OpenXrSystem* xrSystemConcrete = xrSystem.get();
        mXrSystem = std::move(xrSystem);
        lut::VulkanCreateRequirements xrVulkanCreateRequirements{};

        if (mXrSystem) {
            ReportProgress(0.04f, "Initializing OpenXR runtime...");
            mXrSystem->Initialize(nullptr);

            XrVulkanRequirements xrRequirements{};
            if (mXrSystem->TryGetVulkanRequirements(xrRequirements)) {
                xrVulkanCreateRequirements.apiVersion = xrRequirements.apiVersion;
                xrVulkanCreateRequirements.instanceExtensions = xrRequirements.instanceExtensions;
                xrVulkanCreateRequirements.deviceExtensions = xrRequirements.deviceExtensions;
                if (xrSystemConcrete) {
                    xrVulkanCreateRequirements.createInstanceOverride =
                        [xrSystemConcrete](VkInstanceCreateInfo const& createInfo, VkInstance& outInstance, VkResult& outVkResult) {
                            return xrSystemConcrete->CreateVulkanInstanceWithXr(createInfo, outInstance, outVkResult);
                        };
                    xrVulkanCreateRequirements.selectPhysicalDeviceOverride =
                        [xrSystemConcrete](VkInstance instance, VkPhysicalDevice& outPhysicalDevice) {
                            return xrSystemConcrete->GetVulkanGraphicsDeviceWithXr(instance, outPhysicalDevice);
                        };
                    xrVulkanCreateRequirements.createDeviceOverride =
                        [xrSystemConcrete](VkPhysicalDevice physicalDevice, VkDeviceCreateInfo const& createInfo, VkDevice& outDevice, VkResult& outVkResult) {
                            return xrSystemConcrete->CreateVulkanDeviceWithXr(physicalDevice, createInfo, outDevice, outVkResult);
                        };
                }
            }
        }

        CreateEngineSystems();
        if (renderSystem) {
            renderSystem->SetVulkanCreateRequirements(std::move(xrVulkanCreateRequirements));
        }
        ConfigureRenderProgressBridge();

        ReportProgress(0.10f, "Initializing XR subsystems...");
        for (auto& sys : Systems) {
            sys->Init();
        }
        renderSystem->SetInitProgressCallback({});

        ConnectEngineServices();
        if (mXrSystem) {
            if (auto const* xrWindow = renderSystem ? renderSystem->GetVulkanWindow() : nullptr) {
                mXrSystem->InitializeSession(
                    xrWindow->instance,
                    xrWindow->physicalDevice,
                    xrWindow->device,
                    xrWindow->graphicsFamilyIndex,
                    0
                );
            }
            mXrSystem->SetEnabled(mXrSystem->IsAvailable());
            std::println("[XR] {}", mXrSystem->GetStatusText());
        }

        InitializeAudio();
        SubscribeGameplayEvents();
        LoadInitialScene();
        ReportProgress(1.0f, "Ready");
    }

    void Application::ConnectEngineServices()
    {
        ReportProgress(0.68f, "Connecting engine services...");
        if (inputSystem && renderSystem) {
            inputSystem->SetWindow(renderSystem->GetGLFWWindow());
            renderSystem->SetInputSystem(inputSystem);
            physicsSystem->SetInputSystem(inputSystem);
        }

        if (renderSystem) {
            renderSystem->SetXrSystem(mXrSystem.get());
        }
    }

    void Application::InitializeAudio()
    {
        ReportProgress(0.74f, "Loading startup audio...");
        if (!audioSystem) {
            return;
        }

        audioSystem->LoadSound("BackgroundTestMusic", "Assets/Sounds/Looping_radio_mix.mp3");
        audioSystem->SetVolume("BackgroundTestMusic", 0.1f);
        audioSystem->SetPitch("BackgroundTestMusic", 1.0f);

        audioSystem->LoadSound("BikeChain", "Assets/Sounds/BikeChain.mp3");
        audioSystem->SetVolume("BikeChain", 0.2f);
        audioSystem->SetRuntimeVolume("BikeChain", 0.0f);
        audioSystem->SetPitch("BikeChain", 1.0f);
        audioSystem->PlayLoop("BikeChain");
        audioSystem->LoadSound("Chain", "Assets/Sounds/BikeChain.mp3");
    }

    void Application::SubscribeGameplayEvents()
    {
        eventSystem->Subscribe(EventType::Collision, [this](Event& e) {
            auto& collisionE = static_cast<CollisionEvent&>(e);
            (void)collisionE;
        });
    }

    void Application::LoadInitialScene()
    {
        ReportProgress(0.82f, "Loading level...");
        m_currentScene = std::make_unique<level>();
        m_currentScene->Init(renderSystem, sceneManager, physicsSystem, inputSystem, eventSystem, &mState, animationSystem, audioSystem);
    }

    Application::~Application() {
        if (mXrSystem) {
            mXrSystem->Shutdown();
            mXrSystem.reset();
        }

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
        StartStartupAudio();

        while (Running) {
            float dt = std::min(CalcDeltaTime(), kMaxDt);

            if (mXrSystem) {
                mXrSystem->PollEvents();
                bool const xrFrameBegan = mXrSystem->BeginFrame();
                if (!mXrViewValidationLogged) {
                    engine::XrFrameState xrFrameState{};
                    if (mXrSystem->TryGetFrameState(xrFrameState) && xrFrameState.hasValidViews) {
                        std::println("[XR] {}", mXrSystem->GetStatusText());
                        mXrViewValidationLogged = true;
                    }
                }
                (void)xrFrameBegan;
            }

            if (inputSystem) {
                inputSystem->ClearInjectedInputs();
                if (mXrSystem) {
                    mXrSystem->ApplyInputState(*inputSystem);
                }
                inputSystem->Update(dt);
            }

            if (m_currentScene) {
                m_currentScene->Update(dt);
            }

            for (auto& sys : Systems) {
                if (sys.get() == inputSystem) {
                    continue;
                }
                sys->Update(dt);
            }

            if (audioSystem) {
                float speed01 = std::clamp(mState.bikeSpeed / 40.0f, 0.0f, 1.0f);
                audioSystem->SetRuntimeVolume("BikeChain", speed01);
                audioSystem->SetPitch("BikeChain", 0.75f + speed01 * 1.25f);
            }

            if (mXrSystem) {
                mXrSystem->EndFrame();
            }
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

    void Application::StartStartupAudio() {
        if (!audioSystem || mStartupAudioStarted) {
            return;
        }

        audioSystem->PlayLoop("BackgroundTestMusic");
        audioSystem->PlayLoop("BikeChain");
        mStartupAudioStarted = true;
    }
}
