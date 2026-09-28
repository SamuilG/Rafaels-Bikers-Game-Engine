#include "Runtime/Scene/SceneFrameUpdate.hpp"
#include "Runtime/Core/FrameScheduler.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

using namespace engine;

namespace {
    void Require(bool condition, const std::string& message) {
        if (!condition) {
            std::fprintf(stderr, "FAIL: %s\n", message.c_str());
            std::exit(1);
        }
    }

    void Near(double actual, double expected, const char* message) {
        Require(std::isfinite(actual) && std::abs(actual - expected) < 0.000001,
            std::string(message) + " actual=" + std::to_string(actual) + " expected=" + std::to_string(expected));
    }

    struct LocalPosition { float x = 0; };
    struct WorldPosition { float x = 0; };

    struct Scene {
        flecs::world world;
        int simulationCalls = 0;
        int intervalCalls = 0;
        int transformCalls = 0;
        float transformDt = -1;
        flecs::system transformSystem;
        flecs::entity editedEntity;

        Scene() {
            world.system("OrdinarySimulation").run([this](flecs::iter&) { ++simulationCalls; });
            world.system("IntervalSimulation").interval(0.1f)
                .run([this](flecs::iter&) { ++intervalCalls; });
            transformSystem = world.system<const LocalPosition, WorldPosition>("TransformPropagation")
                .each([this](flecs::iter& it, size_t, const LocalPosition& local, WorldPosition& output) {
                    ++transformCalls;
                    transformDt = it.delta_time();
                    output.x = local.x;
                });
            editedEntity = world.entity("EditorMovedEntity")
                .set<LocalPosition>({1.0f}).set<WorldPosition>({-1.0f});
        }

        void Advance(const FrameExecution& frame, int count) {
            for (int index = 0; index < count; ++index)
                UpdateSceneWorld(world, transformSystem, frame, 0.05f);
        }

        void CheckStopped(const FrameExecution& frame) {
            Require(!frame.runSimulationSystems, "test must supply a stopped production frame plan");
            const auto before = *world.get_info();
            const int beforeSimulation = simulationCalls;
            const int beforeInterval = intervalCalls;
            const int beforeTransform = transformCalls;
            // A full second of paused updates would fire many timer callbacks if
            // world.progress were mistakenly called, even with time_scale = 0.
            for (int index = 0; index < 20; ++index) {
                const float position = 20.0f + static_cast<float>(index);
                editedEntity.set<LocalPosition>({position});
                UpdateSceneWorld(world, transformSystem, frame, 0.05f);
                Near(editedEntity.get<WorldPosition>().x, position,
                    "editor transform edits still propagate in a stopped frame");
                Near(transformDt, 0.0, "stopped transform propagation receives zero simulation time");
            }
            Require(simulationCalls == beforeSimulation && intervalCalls == beforeInterval,
                "stopped frames call neither ordinary simulation systems nor interval timers");
            Require(transformCalls == beforeTransform + 20, "only transform propagation runs once per stopped frame");
            const auto after = *world.get_info();
            Near(after.world_time_total, before.world_time_total, "scaled world clock remains frozen");
            Near(after.world_time_total_raw, before.world_time_total_raw, "raw timer clock remains frozen");
            Require(after.frame_count_total == before.frame_count_total,
                "paused editor updates never enter the Flecs world progression pipeline");
        }
    };

    FrameExecution BeginWithoutReload(GameFlowController& flow) {
        return FrameScheduler::BeginFrame(flow, [] {
            Require(false, "unexpected reload request");
            return false;
        });
    }

    void TestPauseFreezesWorldAndResumesTimers() {
        Scene scene;
        GameFlowController flow;
        Require(flow.Request(GameFlowCommand::Start), "queue start");
        auto frame = BeginWithoutReload(flow);
        scene.Advance(frame, 10);
        Require(scene.simulationCalls == 10 && scene.intervalCalls > 0 && scene.transformCalls == 10,
            "real world progression executes simulation, interval timers and transforms before pause");
        Require(flow.Request(GameFlowCommand::Pause), "queue pause");
        frame = BeginWithoutReload(flow);
        scene.CheckStopped(frame);
        const auto beforeResume = *scene.world.get_info();
        const int beforeSimulation = scene.simulationCalls;
        const int beforeInterval = scene.intervalCalls;
        Require(flow.Request(GameFlowCommand::Resume), "queue resume");
        frame = BeginWithoutReload(flow);
        scene.Advance(frame, 4);
        Require(scene.simulationCalls == beforeSimulation + 4 && scene.intervalCalls > beforeInterval,
            "ordinary systems and interval timers both resume with Playing simulation");
        Near(scene.world.get_info()->world_time_total - beforeResume.world_time_total, 0.2,
            "resume advances only supplied simulation time without catching up paused wall time");
        Near(scene.world.get_info()->delta_time_raw, 0.05,
            "interval timers receive the supplied frame delta on resume");
        std::puts("PASS real Flecs pause freezes ordinary systems, interval timers and both clocks while editor transforms propagate");
    }

    void TestMenuAndSettingsKeepOnlyTransformsAlive() {
        Scene scene;
        GameFlowController flow;
        scene.CheckStopped(BeginWithoutReload(flow));
        Require(flow.Request(GameFlowCommand::Start), "queue start from menu");
        scene.Advance(BeginWithoutReload(flow), 3);
        Require(flow.Request(GameFlowCommand::OpenSettings), "queue independent settings pause");
        const auto settings = BeginWithoutReload(flow);
        Require(settings.flowState == GameFlowState::Playing && settings.settingsOpen,
            "Settings pauses a flow that remains nominally Playing");
        scene.CheckStopped(settings);
        Require(flow.Request(GameFlowCommand::CloseSettings), "queue settings close");
        const int before = scene.simulationCalls;
        scene.Advance(BeginWithoutReload(flow), 1);
        Require(scene.simulationCalls == before + 1, "closing settings resumes actual ECS simulation");
        std::puts("PASS MainMenu and Playing-with-Settings allow transform editing without ECS simulation or timer updates");
    }

    void TestReloadCompletionKeepsWorldStopped() {
        Scene scene;
        GameFlowController flow;
        Require(flow.Request(GameFlowCommand::Start), "queue first scene start");
        scene.Advance(BeginWithoutReload(flow), 3);
        Require(flow.Request(GameFlowCommand::Restart), "queue a successful scene rebuild");
        int reloads = 0;
        const auto reloaded = FrameScheduler::BeginFrame(flow, [&] { ++reloads; return true; });
        Require(reloads == 1 && flow.CanSimulate() && reloaded.flowState == GameFlowState::Playing &&
            reloaded.reloadHandled && !reloaded.runSimulationSystems,
            "successful reload publishes Playing but still suppresses the completion frame");
        scene.CheckStopped(reloaded);
        const int before = scene.simulationCalls;
        scene.Advance(BeginWithoutReload(flow), 1);
        Require(scene.simulationCalls == before + 1 && reloads == 1,
            "world resumes on the following frame without consuming the reload again");
        std::puts("PASS successful reload's Playing state cannot bypass its stopped ECS frame gate");
    }
}

int main() {
    TestPauseFreezesWorldAndResumesTimers();
    TestMenuAndSettingsKeepOnlyTransformsAlive();
    TestReloadCompletionKeepsWorldStopped();
    std::puts("PASS all production SceneFrameUpdate regressions (real Flecs world, no GPU/window)");
}
