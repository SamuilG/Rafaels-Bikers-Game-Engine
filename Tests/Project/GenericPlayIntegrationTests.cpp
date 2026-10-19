#include "Runtime/Core/Application.hpp"
#include "Runtime/Renderer/RenderSystem.hpp"
#include "Runtime/Scene/blank.hpp"
#include "Runtime/Scene/SceneManager.hpp"
#include "Runtime/Scene/EditorSceneAdapter.hpp"
#include "Runtime/Project/SceneDocument.hpp"
#include "Runtime/Project/ProjectDescriptor.hpp"
#include "Projects/Bikers/BikersProject.hpp"
#include "Runtime/AudioSystem/AudioSystem.hpp"
#include "Runtime/UserState/GameplayState.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using namespace engine;
using Json = nlohmann::json;
namespace fs = std::filesystem;

void Require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

std::string Utf8(const fs::path& path) {
    const auto text = path.u8string();
    return {reinterpret_cast<const char*>(text.data()), text.size()};
}

void EqualDocument(const Json& expected, const Json& actual, const std::string& path = "scene") {
    if (expected.is_number() && actual.is_number()) {
        const double a = expected.get<double>(), b = actual.get<double>();
        Require(std::abs(a - b) <= 1e-4 * std::max({1.0, std::abs(a), std::abs(b)}),
            path + " differs: " + expected.dump() + " vs " + actual.dump());
        return;
    }
    Require(expected.type() == actual.type(), path + " has a different JSON type");
    if (expected.is_object()) {
        Require(expected.size() == actual.size(), path + " has a different field count");
        for (auto it = expected.begin(); it != expected.end(); ++it) {
            Require(actual.contains(it.key()), path + " is missing " + it.key());
            EqualDocument(it.value(), actual.at(it.key()), path + "/" + it.key());
        }
    } else if (expected.is_array()) {
        Require(expected.size() == actual.size(), path + " has a different item count");
        for (std::size_t i = 0; i < expected.size(); ++i)
            EqualDocument(expected.at(i), actual.at(i), path + "/" + std::to_string(i));
    } else {
        Require(expected == actual, path + " differs: " + expected.dump() + " vs " + actual.dump());
    }
}

enum class Phase {
    Start, AwaitPlay, Simulating, AwaitPause, Paused, AwaitResume,
    AwaitRestart, AwaitRestartUpdate, AwaitStop, AwaitSecondPlay, AwaitFinalStop, Done
};

struct Context {
    RenderSystem* renderer = nullptr;
    SceneManager* authoring = nullptr;
    SceneManager* runtime = nullptr;
    GameplayState* gameplay = nullptr;
    Json authored;
    Json paused;
    Phase phase = Phase::Start;
    int phaseFrames = 0;
    int sceneInitializations = 0;
    int firstSimulationUpdates = 0;
    int countBeforeRestart = 0;
    flecs::entity_t authoredMesh = 0;
    flecs::entity_t authoredCamera = 0;
    uint32_t authoredBody = JPH::BodyID::cInvalidBodyID;
    std::string dynamicIdentity;
    float initialDynamicY = 0.0f;
    glm::mat4 pausedCamera{1.0f};
    std::chrono::steady_clock::time_point started = std::chrono::steady_clock::now();
};

// Exercise the production Application and blank scene without test-only engine
// entry points. The wrapper observes the first gameplay phase before physics.
class ObservedScene final : public GameScene {
public:
    explicit ObservedScene(Context& context) : mContext(context) {}

    void Init(RenderSystem* renderer, SceneManager* scene, PhysicsSystem* physics,
        InputSystem* input, EventSystem* events, GameplayState* state,
        AnimationSystem* animation, AudioSystem* audio) override {
        mBlank.Init(renderer, scene, physics, input, events, state, animation, audio);
        mScene = scene;
        mContext.gameplay = state;
        if (!mContext.authoring) mContext.authoring = scene;
        else mContext.runtime = scene;
        ++mContext.sceneInitializations;
    }

    void Update(float dt) override {
        if (mFirstUpdate) {
            Require(mScene != mContext.authoring, "Play reused the authoring SceneManager");
            EqualDocument(mContext.authored, CaptureSceneDocument(*mScene, *mContext.renderer));
            Require(mContext.renderer->GetGameCameraState().cameraFov == 63.0f,
                "Play did not apply the authored camera lens");
            ++mContext.firstSimulationUpdates;
            mFirstUpdate = false;
        }
        mBlank.Update(dt);
    }

    void Shutdown() override { mBlank.Shutdown(); }

private:
    Context& mContext;
    SceneManager* mScene = nullptr;
    blank mBlank;
    bool mFirstUpdate = true;
};

flecs::entity EntityByIdentity(SceneManager& scene, const std::string& id) {
    flecs::entity found = flecs::entity::null();
    scene.get_world().each([&](flecs::entity entity, const SceneEntityIdentity& identity) {
        if (identity.value == id) found = entity;
    });
    Require(IsEntityAlive(found), "A persisted entity identity was lost: " + id);
    return found;
}

void SeedAuthoring(Context& context) {
    auto& scene = *context.authoring;
    auto& renderer = *context.renderer;
    auto sun = scene.get_world().lookup("MainSun");
    Require(IsEntityAlive(sun) && sun.has<LightComponent>(), "Empty editor does not expose a default directional entity");
    auto light = sun.get<LightComponent>();
    light.direction = NormalizeLightDirection({-1.0f, -0.6f, 0.2f});
    light.shadow.firstSplit = 6; light.shadow.secondSplit = 35; light.shadow.distance = 350;
    light.shadow.receiverBias = 0.001f; light.shadow.filterRadius = 3.2f; light.shadow.strength = 0.6f;
    light.shadow.constantBias = 2; light.shadow.slopeBias = 3;
    sun.set<LightComponent>(light);
    const auto listed = EditorSceneAdapter(&scene).ListEntities();
    Require(std::any_of(listed.begin(), listed.end(), [&](const auto& item) { return item.id == sun.id(); }),
        "Directional light is missing from the production entity list");
    const auto snapshot = fs::absolute("Intermediate/Diagnostics/generic-play-integration/light.snapshot.json");
    Require(EngineUi::SaveProject(&scene, &renderer, Utf8(snapshot)), "Native light snapshot save failed");
    sun.get_mut<LightComponent>().intensity = 9;
    sun.get_mut<LightComponent>().shadow.distance = 950;
    Require(EngineUi::LoadProject(&scene, &renderer, Utf8(snapshot)), "Native light snapshot load failed");
    Require(SerializeLight(sun.get<LightComponent>()) == SerializeLight(light), "Native light snapshot lost source or shadow settings");
    const auto illumination = scene.GetLightingData();
    Require(illumination.directional.entityId == sun.id() && !illumination.lights.empty() &&
        illumination.lights.front().position == illumination.directional.light.position,
        "Actual ECS collection gives illumination and shadows different sun directions");
    std::puts("PASS default directional entity, actual hierarchy listing and native source/shadow snapshot round trip");
    // The same source imported as Static and Dynamic must not reuse a Flecs
    // entity just because their independent generated-name counters coincide.
    const auto staticBeacon = scene.LoadModel(&renderer, "Assets/Models/Beacon.glb", ModelPhysicsType::Static,
        1.0f, glm::translate(glm::mat4(1.0f), {10.0f, 0.0f, 0.0f}));
    const auto first = scene.LoadModel(&renderer, "Assets/Models/tbike.glb", ModelPhysicsType::Static,
        1.0f, glm::translate(glm::mat4(1.0f), {-3.0f, 0.0f, 0.0f}));
    Require(IsEntityAlive(first), "First model import failed");
    const std::string firstImport = first.get<ModelPartSource>().instance;
    std::vector<flecs::entity> importedParts;
    scene.get_world().each([&](flecs::entity entity, const ModelPartSource& source) {
        if (source.instance == firstImport) importedParts.push_back(entity);
    });
    Require(importedParts.size() > 1, "The regression fixture must contain multiple imported meshes");
    scene.DestroyEntity(importedParts.back());

    const auto repeated = scene.LoadModel(&renderer, "Assets/Models/tbike.glb", ModelPhysicsType::Static,
        1.0f, glm::translate(glm::mat4(1.0f), {3.0f, 0.0f, 0.0f}));
    const auto dynamic = scene.LoadModel(&renderer, "Assets/Models/Beacon.glb", ModelPhysicsType::Dynamic,
        3.0f, glm::translate(glm::mat4(1.0f), {0.0f, 10.0f, 0.0f}));
    Require(IsEntityAlive(repeated) && IsEntityAlive(dynamic), "Repeated or dynamic model import failed");
    Require(IsEntityAlive(staticBeacon) && staticBeacon != dynamic && staticBeacon.has<StaticObject>() &&
        !staticBeacon.has<DynamicObject>(), "Mixed static/dynamic imports reused an existing entity");
    context.dynamicIdentity = dynamic.get<SceneEntityIdentity>().value;
    context.initialDynamicY = dynamic.get<LocalTransform>().matrix[3].y;

    const auto parent = scene.get_world().entity("AuthoredParent")
        .set<LocalTransform>({glm::mat4(1.0f)})
        .set<WorldTransform>({glm::mat4(1.0f)});
    repeated.child_of(parent);
    repeated.set<EntityStatus>({false, true});
    context.authoredMesh = repeated.id();
    context.authoredBody = repeated.get<PhysicsBody>().bodyID;

    scene.create_light_entity("AuthoredLight", LightType::Point, {0.2f, 0.6f, 0.9f}, 2.5f,
        glm::translate(glm::mat4(1.0f), {0.0f, 4.0f, 1.0f}), 14.0f,
        {0.0f, 0.0f, -1.0f}, 12.5f, 17.5f, parent);
    const auto camera = FindGameCameraEntity(scene.get_world());
    context.authoredCamera = camera.id();
    auto& component = camera.get_mut<GameCameraComponent>();
    component.settings.fov = 63.0f;
    component.settings.nearPlane = 0.15f;
    component.settings.farPlane = 400.0f;
    component.freeTransform = glm::inverse(glm::lookAt(glm::vec3(0.0f, 4.0f, 12.0f),
        glm::vec3(0.0f, 2.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f)));
    component.settingsDirty = true;
    camera.modified<GameCameraComponent>();

    renderer.AddParticleGroup(32);
    auto& particles = *renderer.GetParticles().back();
    std::strcpy(particles.config.name, "AuthoredParticles");
    particles.config.emitterPos = {2.0f, 3.0f, 1.0f};
    particles.config.startColor = {0.2f, 0.4f, 1.0f, 0.8f};
    particles.config.endColor = {0.8f, 0.4f, 0.2f, 0.0f};
    particles.setEmitterShape(EmitterShape::Box);
    context.authored = CaptureSceneDocument(scene, renderer);
    ValidateSceneDocument(context.authored);
}

void CheckAuthoringRestored(Context& context) {
    EqualDocument(context.authored, CaptureSceneDocument(*context.authoring, *context.renderer));
    const auto mesh = context.authoring->get_world().entity(context.authoredMesh);
    Require(IsEntityAlive(mesh), "Stop did not retain the original authoring entity handle");
    Require(mesh.get<PhysicsBody>().bodyID == context.authoredBody,
        "Stop rebuilt the original authoring physics body");
    Require(FindGameCameraEntity(context.authoring->get_world()).id() == context.authoredCamera,
        "Stop did not retain the original camera entity");
    Require(context.renderer->GetGameCameraState().cameraFov == 63.0f,
        "Stop leaked runtime lens edits into the authoring camera");
}

void DriveTransport(Context& context) {
    Require(std::chrono::steady_clock::now() - context.started < std::chrono::seconds(60),
        "Generic Play integration exceeded its frame timeout");
    auto& play = *context.renderer->GetEditorPlayController();
    Require(play.Error().empty(), "Play failed: " + play.Error());
    switch (context.phase) {
    case Phase::Start:
        Require(play.Available() && !play.IsPlaying(), "A new generic workspace is not runnable");
        EqualDocument(context.authored, CaptureSceneDocument(*context.authoring, *context.renderer));
        Require(play.RequestPlay(), "Play request rejected");
        context.phase = Phase::AwaitPlay;
        break;
    case Phase::AwaitPlay:
        Require(play.IsSimulating() && context.firstSimulationUpdates == 1,
            "Play did not instantiate the authored scene before simulation");
        context.phaseFrames = 0;
        context.phase = Phase::Simulating;
        break;
    case Phase::Simulating:
        if (++context.phaseFrames < 4) break;
        Require(EntityByIdentity(*context.runtime, context.dynamicIdentity).get<LocalTransform>().matrix[3].y <
            context.initialDynamicY - 0.0001f, "Generic Play did not simulate the imported dynamic body");
        Require(play.RequestPause(), "Pause request rejected");
        context.phase = Phase::AwaitPause;
        break;
    case Phase::AwaitPause:
        Require(play.IsPaused(), "Pause did not commit");
        context.paused = CaptureSceneDocument(*context.runtime, *context.renderer);
        context.pausedCamera = context.renderer->GetGameCameraState().camera2world;
        context.phaseFrames = 0;
        context.phase = Phase::Paused;
        break;
    case Phase::Paused:
        EqualDocument(context.paused, CaptureSceneDocument(*context.runtime, *context.renderer));
        Require(context.renderer->GetGameCameraState().camera2world == context.pausedCamera,
            "Paused runtime camera continued moving");
        if (++context.phaseFrames < 3) break;
        Require(play.RequestPause(), "Resume request rejected");
        context.phase = Phase::AwaitResume;
        break;
    case Phase::AwaitResume: {
        Require(play.IsSimulating(), "Resume did not commit");
        // Mutate disposable runtime authoring fields too. Restart must restore
        // the frozen Play document, rather than recapturing the live world.
        const auto camera = FindGameCameraEntity(context.runtime->get_world());
        camera.get_mut<GameCameraComponent>().settings.fov = 91.0f;
        camera.get_mut<GameCameraComponent>().settingsDirty = true;
        camera.modified<GameCameraComponent>();
        context.renderer->GetParticles().front()->config.emitterPos = {100.0f, 200.0f, 300.0f};
        context.countBeforeRestart = context.sceneInitializations;
        Require(context.gameplay->gameFlow.Request(GameFlowCommand::Restart), "Restart request rejected");
        context.phase = Phase::AwaitRestart;
        break;
    }
    case Phase::AwaitRestart:
        Require(context.sceneInitializations == context.countBeforeRestart + 1,
            "Restart did not reconstruct the runtime scene");
        EqualDocument(context.authored, CaptureSceneDocument(*context.runtime, *context.renderer));
        context.phase = Phase::AwaitRestartUpdate;
        break;
    case Phase::AwaitRestartUpdate:
        Require(context.firstSimulationUpdates == 2, "Restart did not resume the reconstructed scene");
        Require(play.RequestStop(), "Stop request rejected");
        context.phase = Phase::AwaitStop;
        break;
    case Phase::AwaitStop:
        Require(!play.IsPlaying(), "Stop did not commit");
        CheckAuthoringRestored(context);
        Require(play.RequestPlay(), "A second Play request failed");
        context.phase = Phase::AwaitSecondPlay;
        break;
    case Phase::AwaitSecondPlay:
        Require(play.IsSimulating() && context.firstSimulationUpdates == 3,
            "A second Play did not reconstruct the original authored scene");
        Require(play.RequestStop(), "Second Stop request rejected");
        context.phase = Phase::AwaitFinalStop;
        break;
    case Phase::AwaitFinalStop:
        Require(!play.IsPlaying(), "Second Stop did not commit");
        CheckAuthoringRestored(context);
        context.phase = Phase::Done;
        glfwSetWindowShouldClose(context.renderer->GetGLFWWindow(), GLFW_TRUE);
        break;
    case Phase::Done:
        break;
    }
}

ApplicationOptions Options(Context& context, const fs::path& outputDirectory) {
    ApplicationOptions options;
    options.createScene = [&context]() { return std::make_unique<ObservedScene>(context); };
    options.editorAvailable = true;
    options.showEditorInitially = true;
    options.editingProject = true;
    options.playInEditorAvailable = true;
    options.genericScene = true;
    options.imguiIniPath = Utf8(outputDirectory / "integration-layout.ini");
    options.windowTitle = "Generic scene integration test (hidden)";
    options.configureRenderer = [&context](RenderSystem& renderer) { context.renderer = &renderer; };
    return options;
}

void TestPlayAndDocumentReopen(const fs::path& outputDirectory) {
    Context context;
    Json authored;
    const auto savedFile = outputDirectory / fs::u8path("通用场景回归.scene.json");
    {
        auto options = Options(context, outputDirectory);
        options.updateAudio = [&context](AudioSystem&, const PlayerState&, const FrameExecution&) {
            DriveTransport(context);
        };
        Application application(std::move(options));
        // Application creates a hidden GLFW window; do not call ShowMainWindow.
        SeedAuthoring(context);
        authored = context.authored;
        std::ofstream saved(savedFile);
        saved << authored.dump(2);
        saved.close();
        Require(!saved.fail(), "Cannot write the integration scene document");
        application.Run();
        Require(context.phase == Phase::Done, "The integration app exited before completing transport checks");
    }
    std::cout << "[PASS] imported meshes, deletion, hierarchy, camera, lights and particles survive Play\n"
        << "[PASS] physics advances, Pause freezes, Restart restores the Play document\n"
        << "[PASS] Stop retains original unsaved entities, physics handles and resources; second Play works\n";

    Context malformed;
    const auto brokenFile = outputDirectory / "malformed.scene.json";
    { std::ofstream broken(brokenFile); broken << "{\"schemaVersion\":1}"; }
    bool rejected = false;
    try {
        auto options = Options(malformed, outputDirectory);
        options.sceneSnapshotPath = Utf8(brokenFile);
        Application invalidApplication(std::move(options));
    } catch (const std::runtime_error& error) {
        rejected = std::string(error.what()).find("Scene document") != std::string::npos;
    }
    Require(rejected, "A malformed scene document was not rejected during construction");

    Context reopened;
    {
        auto options = Options(reopened, outputDirectory);
        options.sceneSnapshotPath = Utf8(savedFile);
        Application application(std::move(options));
        EqualDocument(authored, CaptureSceneDocument(*reopened.authoring, *reopened.renderer));
        Require(reopened.renderer->GetGameCameraState().cameraFov == 63.0f,
            "Reopening did not restore authored camera settings");
    }
    std::cout << "[PASS] malformed document construction cleans up; a fresh Application reloads a Unicode scene path\n";
}

struct NativeContext {
    RenderSystem* renderer = nullptr;
    SceneManager* authoring = nullptr;
    SceneManager* runtime = nullptr;
    int initialized = 0;
    int updates = 0;
    int phase = 0;
    int pausedFrames = 0;
    int updatesAtPause = 0;
    int originalEntities = 0;
    flecs::entity_t originalCompound = 0;
    uint32_t originalBody = JPH::BodyID::cInvalidBodyID;
    std::chrono::steady_clock::time_point started = std::chrono::steady_clock::now();
};

class ObservedNativeScene final : public GameScene {
public:
    ObservedNativeScene(NativeContext& context, std::unique_ptr<GameScene> scene)
        : mContext(context), mScene(std::move(scene)) {}
    void Init(RenderSystem* renderer, SceneManager* scene, PhysicsSystem* physics,
        InputSystem* input, EventSystem* events, GameplayState* state,
        AnimationSystem* animation, AudioSystem* audio) override {
        mScene->Init(renderer, scene, physics, input, events, state, animation, audio);
        if (!mContext.authoring) mContext.authoring = scene;
        else mContext.runtime = scene;
        ++mContext.initialized;
    }
    void Update(float dt) override { ++mContext.updates; mScene->Update(dt); }
    void RefreshPlayerMotion(float dt) override { mScene->RefreshPlayerMotion(dt); }
    void RefreshEditorCameraPreview() override { mScene->RefreshEditorCameraPreview(); }
    void UpdatePresentation(float dt) override { mScene->UpdatePresentation(dt); }
    void Shutdown() override { mScene->Shutdown(); }
private:
    NativeContext& mContext;
    std::unique_ptr<GameScene> mScene;
};

void TestNativeBikersCompatibility(const fs::path& outputDirectory) {
    NativeContext context;
    const auto descriptor = ProjectDescriptor::Load("Projects/Bikers/Bikers.steerproj");
    auto options = bikers::CreateOptions(descriptor, true);
    Require(!options.genericScene, "Native Bikers was routed through generic scene reconstruction");
    const auto factory = options.createScene;
    options.createScene = [&context, factory] { return std::make_unique<ObservedNativeScene>(context, factory()); };
    const auto configure = options.configureRenderer;
    options.configureRenderer = [&context, configure](RenderSystem& renderer) {
        configure(renderer);
        context.renderer = &renderer;
    };
    const auto initializeAudio = options.initializeAudio;
    options.initializeAudio = [initializeAudio](AudioSystem& audio) {
        initializeAudio(audio);
        audio.SetMasterVolume(0.0f);
    };
    // Test the shipped native factory without reading/writing a user's local
    // scene snapshot or dock layout from an existing Bikers editing session.
    options.sceneSnapshotPath.clear();
    options.imguiIniPath = Utf8(outputDirectory / "native-layout.ini");
    const auto updateAudio = options.updateAudio;
    options.updateAudio = [&context, updateAudio](AudioSystem& audio, const PlayerState& player, const FrameExecution& frame) {
        updateAudio(audio, player, frame);
        Require(std::chrono::steady_clock::now() - context.started < std::chrono::seconds(90),
            "Native Bikers smoke test timed out");
        auto& play = *context.renderer->GetEditorPlayController();
        Require(play.Error().empty(), "Native Bikers Play failed: " + play.Error());
        if (context.phase == 0) {
            Require(context.originalEntities > 100 && context.updates == 0, "Native editor scene is missing or simulated early");
            Require(play.RequestPlay(), "Native Play request rejected");
            context.phase = 1;
        } else if (context.phase == 1) {
            Require(play.IsSimulating() && context.initialized == 2 && context.runtime != context.authoring,
                "Native Play did not use an isolated native scene factory");
            Require(context.runtime->get_entity_count() >= context.originalEntities,
                "Native Play lost authored native entities");
            if (context.updates < 3) return;
            Require(play.RequestPause(), "Native Pause request rejected");
            context.phase = 2;
            context.updatesAtPause = context.updates;
        } else if (context.phase == 2) {
            Require(play.IsPaused() && context.updates == context.updatesAtPause,
                "Native scene continued updating while editor transport was paused");
            if (++context.pausedFrames < 2) return;
            Require(play.RequestStop(), "Native Stop request rejected");
            context.phase = 3;
        } else if (context.phase == 3) {
            Require(!play.IsPlaying() && context.authoring->get_entity_count() == context.originalEntities,
                "Native Stop did not restore the authoring scene");
            const auto compound = context.authoring->get_world().entity(context.originalCompound);
            Require(IsEntityAlive(compound) && compound.get<CompoundParent>().bodyID == context.originalBody,
                "Native Stop did not retain the original compound body");
            context.phase = 4;
            glfwSetWindowShouldClose(context.renderer->GetGLFWWindow(), GLFW_TRUE);
        }
    };
    Application application(std::move(options));
    context.originalEntities = context.authoring->get_entity_count();
    context.authoring->get_world().each([&](flecs::entity entity, const CompoundParent& compound) {
        if (!context.originalCompound) { context.originalCompound = entity.id(); context.originalBody = compound.bodyID; }
    });
    Require(context.originalCompound != 0, "Native Bikers factory has no compound vehicle body");
    application.Run();
    Require(context.phase == 4, "Native Bikers smoke test exited before Stop");
    std::cout << "[PASS] native Bikers factory, compound vehicle, simulation, Pause and original-world Stop remain compatible\n";
}
} // namespace

int main(int argc, char** argv) {
    try {
        Require(argc == 2, "Usage: GenericPlayIntegrationTests.exe <output-directory>");
        const auto outputDirectory = fs::absolute(fs::u8path(argv[1]));
        fs::create_directories(outputDirectory);
        TestPlayAndDocumentReopen(outputDirectory);
        TestNativeBikersCompatibility(outputDirectory);
        std::cout << "Generic Play integration checks passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "[FAIL] " << error.what() << '\n';
        return 1;
    }
}
