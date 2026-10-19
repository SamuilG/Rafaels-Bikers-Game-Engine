#include "Application.hpp"
#include "../Renderer/RenderSystem.hpp"
#include "../Scene/SceneManager.hpp"
#include "../Physics/PhysicsSystem.hpp"
#include "../Input/InputSystem.hpp"
#include "../Event/EventSystem.hpp"
#include "../Animation/AnimationSystem.hpp"
#include "../AudioSystem/AudioSystem.hpp"
#include "../UI/VisualUIEditor/RuntimeUiController.hpp"
#include "../Project/SceneDocument.hpp"
#include "../Project/SceneDocumentValidation.hpp"
#include "FrameScheduler.hpp"
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <unordered_set>
#include <Jolt/Physics/Body/MotionProperties.h>

namespace engine {

    struct Application::EditorPlaySession {
        UserState savedState;
        PhysicsSystem* editorPhysics = nullptr;
        SceneManager* editorScene = nullptr;
        EventSystem* editorEvents = nullptr;
        AnimationSystem* editorAnimation = nullptr;
        AudioSystem* editorAudio = nullptr;
        float editorMasterVolume = 1.0f;
        std::unique_ptr<GameScene> editorGameScene;
        RenderSystem::SceneTransientResources editorResources;
        std::unique_ptr<EventSystem> playEvents;
        std::unique_ptr<AudioSystem> playAudio;
        std::unique_ptr<PhysicsSystem> playPhysics;
        std::unique_ptr<SceneManager> playScene;
        std::unique_ptr<AnimationSystem> playAnimation;
        // Frozen at the Play boundary, including edits that have not been saved.
        // A runtime restart uses this document, never the mutated simulation.
        nlohmann::json authoringDocument;
    };

    Application::Application(ApplicationOptions options, ProgressCallback progress)
        : mOptions(std::move(options)), mProgressCallback(std::move(progress))
    {
        if (!mOptions.createScene) throw std::invalid_argument("Application requires a scene factory.");
        if (mOptions.showEditorInitially && !mOptions.editorAvailable)
            throw std::invalid_argument("Editor visibility requires editor availability.");
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
        renderSystem->ConfigureHost(mOptions.editorAvailable, mOptions.showEditorInitially, mOptions.runtimeUiEnabled);
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

        // Once every subsystem is initialized, a bad project document/model
        // can still fail the constructor. Its destructor would not run in
        // that case, so release the scene and initialized services explicitly.
        try {
        renderSystem->SetInitProgressCallback({});
        glfwSetWindowTitle(renderSystem->GetGLFWWindow(), mOptions.windowTitle.c_str());
        if (!mOptions.imguiIniPath.empty()) {
            const auto parent = std::filesystem::u8path(mOptions.imguiIniPath).parent_path();
            if (!parent.empty()) std::filesystem::create_directories(parent);
            ImGui::GetIO().IniFilename = mOptions.imguiIniPath.c_str();
        }
        if (mOptions.configureRenderer) mOptions.configureRenderer(*renderSystem);
        mEditorPlay.Configure(mOptions.editingProject && mOptions.playInEditorAvailable);
        renderSystem->SetEditorPlayController(mOptions.editingProject ? &mEditorPlay : nullptr);
        if (mOptions.startPlaying) mState.gameFlow.Request(GameFlowCommand::Start);

        ReportProgress(0.68f, "Connecting engine services...");
        if (inputSystem && renderSystem) {
            inputSystem->SetWindow(renderSystem->GetGLFWWindow());
            renderSystem->SetInputSystem(inputSystem);

        }

        ReportProgress(0.70f, "Loading application services...");
        RuntimeUiController* runtimeUiController = renderSystem->GetRuntimeUiController();
        if (runtimeUiController && mOptions.editingProject)
            runtimeUiController->SetQuitHandler([this]() { mEditorPlay.RequestStop(); });
        if (runtimeUiController && mOptions.preloadRuntimeUi)
            mOptions.preloadRuntimeUi(*runtimeUiController);
        m_currentScene = CreateScene();
        if (mOptions.initializeAudio) mOptions.initializeAudio(*audioSystem);

        eventSystem->Subscribe(EventType::Collision, [this](Event& e) {
            auto& collisionE = static_cast<CollisionEvent&>(e);
            // ... 你的碰撞日志打印逻辑 ...
            });


        ReportProgress(0.82f, "Loading level...");
        // ==============================================================
        // 【核心】：加载当前关卡 (未来切换关卡，只需要 new 不同的 Scene 即可)
        // ==============================================================
        InitializeCameraDefaults();
        m_currentScene->Init(renderSystem, sceneManager, physicsSystem, inputSystem, eventSystem, &mGameplayState, animationSystem, audioSystem);
        ApplySceneWorkspace();
        sceneManager->Update(0.0f);
        if (mOptions.editingProject) m_currentScene->RefreshEditorCameraPreview();
        RefreshGameCameraEntity();
        animationSystem->Update(0.0f);
        if (runtimeUiController) runtimeUiController->SyncGameFlowUi();
        ReportProgress(1.0f, "Ready");
        } catch (...) {
            if (m_currentScene) {
                try { m_currentScene->Shutdown(); } catch (...) {}
                m_currentScene.reset();
            }
            for (auto it = Systems.rbegin(); it != Systems.rend(); ++it) {
                try { (*it)->Shutdown(); } catch (...) {}
            }
            Systems.clear();
            throw;
        }
    }

    Application::~Application() {
        if (mEditorPlaySession) StopEditorPlay();
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
            .scene = [this](float dt) {
                sceneManager->Update(dt);
                // A paused Play session retains its exact runtime camera.
                // Only the authoring world follows edits without simulation.
                if (mOptions.editingProject && !mEditorPlay.IsPlaying() && m_currentScene)
                    m_currentScene->RefreshEditorCameraPreview();
                RefreshGameCameraEntity();
            },
            .animation = [this](float dt) { animationSystem->Update(dt); },
            .gameEffects = [this](float dt) { if (m_currentScene) m_currentScene->UpdatePresentation(dt); },
            .presentation = [this](float dt) { renderSystem->Update(dt); },
            .audio = [this](float dt) {
                audioSystem->Update(dt);
                if (mOptions.updateAudio)
                    mOptions.updateAudio(*audioSystem, mState.player.State(), mState.frame);
            }
        };

        while (Running) {
            const auto cpuFrameStart = std::chrono::steady_clock::now();
            const float dt = std::min(CalcDeltaTime(), kMaxDt);

            // Sample once, then publish the previous frame's accepted commands.
            inputSystem->Update(dt);
            const bool transportChanged = mEditorPlay.Commit(
                [this]() { return StartEditorPlay(); }, [this]() { StopEditorPlay(); });
            if (transportChanged) {
                renderSystem->ResetPerformanceHistory();
                inputSystem->ResetForNewSession();
                mLastTime = std::chrono::steady_clock::now();
            }
            if (mEditorPlaySession) {
                audioSystem->SetHostMuted(mEditorPlay.IsPaused());
            }
            mState.frame = FrameScheduler::BeginFrame(mState.gameFlow, [this]() {
                try {
                    return ReloadCurrentScene();
                }
                catch (const std::exception& error) {
                    EngineUi::LogPrintf("Engine reload/recovery failed: %s\n", error.what());
                    Running = false;
                    return false;
                }
            }, mOptions.editingProject && !mEditorPlay.IsSimulating());
            if (!Running) break;

            // Editing is a host decision, independent of in-game pause/menu state.
            if (mOptions.editingProject) {
                mState.editor.showEngineUi = true;
            }

            if (mState.frame.reloadHandled) {
                inputSystem->ResetForNewSession();
                mLastTime = std::chrono::steady_clock::now();
            }
            if (auto* ui = renderSystem->GetRuntimeUiController()) ui->SyncGameFlowUi();

            const bool gameplayViewportActive = mOptions.editingProject
                ? renderSystem->GameViewportAcceptsGameplayInput()
                : mState.editor.isSceneViewportHovered;
            const bool editorOwnsKeyboard = mState.editor.showEngineUi &&
                (mState.editor.inputCapturesKeyboard || !gameplayViewportActive);
            inputSystem->SetGameplayInputEnabled(mState.frame.runGameplay && !editorOwnsKeyboard && inputSystem->IsWindowFocused());

            // Every consumer sees this same snapshot until the next boundary.
            // UI/gameplay requests made by these steps cannot change its gates.
            FrameScheduler::Execute(mState.frame, dt, steps);
            // Successful ordinary frames only. Loading, swapchain recreation and
            // Play/Stop transitions are not representative steady-state samples.
            if (renderSystem->HasPresentedFrame() && !transportChanged && !mState.frame.reloadHandled) {
                auto sample = renderSystem->LastFrameTimings();
                sample.totalMs = std::chrono::duration<double, std::milli>(
                    std::chrono::steady_clock::now() - cpuFrameStart).count();
                sample.cpuWorkMs = std::max(0.0, sample.totalMs - sample.fenceWaitMs -
                    sample.acquireWaitMs - sample.presentCallMs);
                mState.renderStats.performance.AddCpuSample(sample);
            }
        }
    }

    std::unique_ptr<GameScene> Application::CreateScene() const {
        auto scene = mOptions.createScene();
        if (!scene) throw std::runtime_error("The application scene factory returned null.");
        return scene;
    }

    void Application::ApplySceneWorkspace() {
        EnsureGameCameraEntity(sceneManager->get_world(), mState.camera);
        if (mEditorPlaySession) {
            ApplyEditorSceneToPlay();
            return;
        }
        // Save As binds a project to the existing authoring world without
        // replacing this Application or its immutable launch options.
        const auto& project = renderSystem->ProjectActions();
        const std::string documentPath = mOptions.genericScene && mOptions.editingProject &&
            !project.snapshotFile.empty() ? project.snapshotFile : mOptions.sceneSnapshotPath;
        if (mOptions.genericScene && !documentPath.empty()) {
            std::ifstream input(std::filesystem::u8path(documentPath));
            if (!input) throw std::runtime_error("Cannot open scene document: " + documentPath);
            nlohmann::json document;
            input >> document;
            ValidateSceneDocument(document);
            InstantiateSceneDocument(document, *sceneManager, *renderSystem);
        } else if (!documentPath.empty() && std::filesystem::exists(std::filesystem::u8path(documentPath))) {
            if (!EngineUi::LoadProject(sceneManager, renderSystem, documentPath))
                throw std::runtime_error("Cannot load project scene snapshot: " + documentPath);
        }
        if (mOptions.editingProject) {
            InitializeDefaultGameCamera(mState.editor.sceneCamera, mState.player.State(),
                mOptions.editorCameraPosition, mOptions.editorCameraTarget);
            mState.editor.showEngineUi = true;
        }
        if (mOptions.genericScene && documentPath.empty() && !sceneManager->GetDirectionalLight().entityId)
            sceneManager->create_light_entity("MainSun", LightType::Directional, {1.2f, 0.95f, 0.8f},
                2.5f, glm::mat4(1), 0, NormalizeLightDirection({0.5f, -1.0f, 0.3f}));
    }

    void Application::ResetRuntimeUi() {
        if (auto* ui = renderSystem->GetRuntimeUiController()) {
            ui->ResetForSession();
            if (mOptions.preloadRuntimeUi) mOptions.preloadRuntimeUi(*ui);
        }
    }

    void Application::InitializeCameraDefaults() {
        InitializeDefaultGameCamera(mState.camera, mState.player.State(),
            mOptions.editorCameraPosition, mOptions.editorCameraTarget);
    }

    void Application::RefreshGameCameraEntity() {
        ApplyGameCameraEdits(sceneManager->get_world(), mState.camera, mState.player.State());
        PublishGameCameraPose(sceneManager->get_world(), mState.camera);
    }

    std::string Application::StartEditorPlay() {
        if (mEditorPlaySession) return "A play session is already active.";
        auto session = std::make_unique<EditorPlaySession>();
        session->savedState = mState;
        session->editorPhysics = physicsSystem;
        session->editorScene = sceneManager;
        session->editorEvents = eventSystem;
        session->editorAnimation = animationSystem;
        session->editorAudio = audioSystem;
        session->editorMasterVolume = audioSystem->GetMasterVolume();
        if (mOptions.genericScene) {
            try {
                // Particle configurations and source model references still
                // belong to the authoring renderer here. Capture before its
                // resources are moved out and before any state is reset.
                session->authoringDocument = CaptureSceneDocument(*sceneManager, *renderSystem);
                ValidateSceneDocument(session->authoringDocument);
            } catch (const std::exception& error) {
                EngineUi::LogPrintf("[Editor] Cannot prepare Play scene: %s\n", error.what());
                return error.what();
            }
        }
        // Complete the potentially throwing GPU wait before moving authoring state.
        renderSystem->WaitForGpuIdle();
        session->editorResources = renderSystem->TakeSceneTransientResources();
        session->editorGameScene = std::move(m_currentScene);
        mEditorPlaySession = std::move(session);
        try {
            auto& active = *mEditorPlaySession;
            active.editorAudio->SetHostMuted(true);
            active.playEvents = std::make_unique<EventSystem>();
            active.playAudio = std::make_unique<AudioSystem>();
            active.playPhysics = std::make_unique<PhysicsSystem>();
            active.playScene = std::make_unique<SceneManager>(active.playPhysics.get());
            active.playAnimation = std::make_unique<AnimationSystem>();
            eventSystem = active.playEvents.get();
            audioSystem = active.playAudio.get();
            physicsSystem = active.playPhysics.get();
            sceneManager = active.playScene.get();
            animationSystem = active.playAnimation.get();
            eventSystem->Init();
            audioSystem->Init();
            audioSystem->SetMasterVolume(active.editorMasterVolume);
            physicsSystem->SetEventSystem(eventSystem);
            physicsSystem->Init();
            sceneManager->SetState(&mSceneState);
            sceneManager->Init();
            animationSystem->set_scene_manager(sceneManager);
            animationSystem->Init();
            renderSystem->SetActiveScene(sceneManager, animationSystem);
            renderSystem->SetAudioSystem(audioSystem);
            mState.ResetSession();
            mState.gameFlow = GameFlowController{};
            ResetRuntimeUi();
            if (mOptions.initializeAudio) mOptions.initializeAudio(*audioSystem);
            m_currentScene = CreateScene();
            InitializeCameraDefaults();
            m_currentScene->Init(renderSystem, sceneManager, physicsSystem, inputSystem,
                eventSystem, &mGameplayState, animationSystem, audioSystem);
            ApplyEditorSceneToPlay();
            sceneManager->Update(0.0f);
            if (mOptions.editingProject) m_currentScene->RefreshEditorCameraPreview();
            RefreshGameCameraEntity();
            animationSystem->Update(0.0f);
            mState.gameFlow.Request(GameFlowCommand::Start);
            mState.editor.focusGameViewport = true;
            EngineUi::LogPrint("[Editor] Play session started; authoring world retained in memory.\n");
            return {};
        } catch (const std::exception& error) {
            const std::string message = error.what();
            StopEditorPlay();
            EngineUi::LogPrintf("[Editor] Play failed: %s\n", message.c_str());
            return message;
        }
    }

    void Application::ApplyEditorSceneToPlay() {
        if (!mEditorPlaySession) return;
        EnsureGameCameraEntity(sceneManager->get_world(), mState.camera);
        auto& saved = *mEditorPlaySession;
        if (mOptions.genericScene) {
            // Instantiate all authored entities and their asset dependencies;
            // the native-scene name matching path below is only for compiled
            // game content whose factory already constructs its entities.
            InstantiateSceneDocument(saved.authoringDocument, *sceneManager, *renderSystem);
            return;
        }
        EditorSceneAdapter runtimeScene(sceneManager);
        std::unordered_set<uint32_t> copiedBodies;
        std::size_t unmatched = 0;
        saved.editorScene->get_world().each([&](flecs::entity source, const LocalTransform& transform) {
            if (source.name().size() == 0) return;
            auto target = sceneManager->get_world().lookup(source.path().c_str());
            if (!IsEntityAlive(target) || !target.has<LocalTransform>()) {
                ++unmatched;
                return;
            }
            if (source.has<GameCameraComponent>()) {
                CopyGameCameraAuthoring(source, target);
                return;
            }
            runtimeScene.SetTransform(target.id(), transform.matrix, false);
            if (source.has<EntityStatus>() && target.has<EntityStatus>())
                target.set<EntityStatus>(source.get<EntityStatus>());
            if (source.has<LightComponent>() && target.has<LightComponent>())
                target.set<LightComponent>(source.get<LightComponent>());
            uint32_t sourceBody = JPH::BodyID::cInvalidBodyID;
            uint32_t targetBody = JPH::BodyID::cInvalidBodyID;
            if (source.has<PhysicsBody>() && target.has<PhysicsBody>()) {
                sourceBody = source.get<PhysicsBody>().bodyID;
                targetBody = target.get<PhysicsBody>().bodyID;
            } else if (source.has<CompoundParent>() && target.has<CompoundParent>()) {
                sourceBody = source.get<CompoundParent>().bodyID;
                targetBody = target.get<CompoundParent>().bodyID;
            }
            if (targetBody != JPH::BodyID::cInvalidBodyID && copiedBodies.insert(targetBody).second) {
                auto& sourceBodies = saved.editorPhysics->get_body_interface();
                auto& targetBodies = physicsSystem->get_body_interface();
                const JPH::BodyID from(sourceBody), to(targetBody);
                JPH::RVec3 position;
                JPH::Quat rotation;
                sourceBodies.GetPositionAndRotation(from, position, rotation);
                // A scene may override mass/inertia (the bike does). Recomputing
                // from shape density would change its handling on every Play.
                targetBodies.SetShape(to, sourceBodies.GetShape(from), false, JPH::EActivation::DontActivate);
                targetBodies.SetPositionAndRotation(to, position, rotation, JPH::EActivation::Activate);
                if (targetBodies.GetMotionType(to) != JPH::EMotionType::Static)
                    targetBodies.SetLinearAndAngularVelocity(to, JPH::Vec3::sZero(), JPH::Vec3::sZero());
                JPH::BodyLockRead sourceLock(saved.editorPhysics->GetJoltSystem()->GetBodyLockInterface(), from);
                JPH::BodyLockWrite targetLock(physicsSystem->GetJoltSystem()->GetBodyLockInterface(), to);
                if (sourceLock.Succeeded() && targetLock.Succeeded() &&
                    sourceLock.GetBody().IsDynamic() && targetLock.GetBody().IsDynamic()) {
                    const auto* sourceMotion = sourceLock.GetBody().GetMotionProperties();
                    auto* targetMotion = targetLock.GetBody().GetMotionProperties();
                    targetMotion->SetInverseMass(sourceMotion->GetInverseMass());
                    targetMotion->SetInverseInertia(sourceMotion->GetInverseInertiaDiagonal(), sourceMotion->GetInertiaRotation());
                    targetMotion->SetLinearDamping(sourceMotion->GetLinearDamping());
                    targetMotion->SetAngularDamping(sourceMotion->GetAngularDamping());
                    targetMotion->SetGravityFactor(sourceMotion->GetGravityFactor());
                }
            }
        });
        auto& particles = renderSystem->GetParticles();
        for (std::size_t i = 0; i < saved.editorResources.particles.size(); ++i) {
            const auto& original = saved.editorResources.particles[i];
            if (!original) continue;
            if (i >= particles.size()) renderSystem->AddParticleGroup(original->count());
            if (particles[i]->count() != original->count())
                renderSystem->ResizeParticleGroup(i, original->count());
            particles[i]->config = original->config;
            particles[i]->setEmitterShape(original->getEmitterShape());
        }
        if (unmatched > 0)
            EngineUi::LogPrintf("[Editor] %zu imported authoring entities are preserved for Stop but are not part of the native play-scene factory.\n", unmatched);
    }

    void Application::StopEditorPlay() {
        if (!mEditorPlaySession) return;
        renderSystem->WaitForGpuIdle();
        auto session = std::move(mEditorPlaySession);
        if (m_currentScene) {
            m_currentScene->Shutdown();
            m_currentScene.reset();
        }
        // Keep current dock/panel state; scene entities and their systems are the
        // original instances, so unsaved additions/deletions never need serialization.
        const auto workspace = mState.editor;
        const auto renderPreferences = mState.render;
        mState = session->savedState;
        mState.editor = workspace;
        mState.render = renderPreferences;
        mState.editor.focusSceneViewport = true;
        physicsSystem = session->editorPhysics;
        sceneManager = session->editorScene;
        eventSystem = session->editorEvents;
        animationSystem = session->editorAnimation;
        audioSystem = session->editorAudio;
        renderSystem->SetActiveScene(sceneManager, animationSystem);
        renderSystem->RestoreSceneTransientResources(std::move(session->editorResources));
        renderSystem->SetAudioSystem(audioSystem);
        audioSystem->SetHostMuted(false);
        m_currentScene = std::move(session->editorGameScene);
        ResetRuntimeUi();
        if (auto* ui = renderSystem->GetRuntimeUiController()) ui->SyncGameFlowUi();
        inputSystem->SetGameplayInputEnabled(false);
        inputSystem->ResetForNewSession();
        // Explicit shutdown also supports partially initialized play sessions.
        if (session->playAnimation) session->playAnimation->Shutdown();
        if (session->playScene) session->playScene->Shutdown();
        if (session->playPhysics) session->playPhysics->Shutdown();
        if (session->playAudio) session->playAudio->Shutdown();
        if (session->playEvents) session->playEvents->Shutdown();
        EngineUi::LogPrint("[Editor] Play stopped; original authoring world restored.\n");
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

        if (m_currentScene) {
            m_currentScene->Shutdown();
            m_currentScene.reset();
        }

        if (renderSystem) {
            renderSystem->ClearSceneTransientResources();
            if (mOptions.genericScene) {
                // Model indices in a reconstructed document start in a fresh
                // resource set. Dispose the old runtime GPU assets after idle;
                // the retained editor world's assets remain in its session.
                auto discardedResources = renderSystem->TakeSceneTransientResources();
            }
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
            if (mOptions.initializeAudio) mOptions.initializeAudio(*audioSystem);
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
            m_currentScene = CreateScene();
            InitializeCameraDefaults();
            m_currentScene->Init(renderSystem, sceneManager, physicsSystem, inputSystem, eventSystem, &mGameplayState, animationSystem, audioSystem);
            ApplySceneWorkspace();
            sceneManager->Update(0.0f);
            if (mOptions.editingProject) m_currentScene->RefreshEditorCameraPreview();
            RefreshGameCameraEntity();
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
