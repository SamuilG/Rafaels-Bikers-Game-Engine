#include "Runtime/Core/FrameScheduler.hpp"
#include "Runtime/Trigger/trigger.hpp"
#include "Runtime/UserState/UserState.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

using namespace engine;

static_assert(std::is_same_v<decltype(std::declval<RendererStateView>().hud), const HudData&>);
static_assert(std::is_same_v<decltype(std::declval<RendererStateView>().effects), const SceneEffects&>);

namespace {
    constexpr float kDt = 0.1f;

    void Require(bool condition, const char* message) {
        if (!condition) {
            std::fprintf(stderr, "FAIL: %s\n", message);
            std::exit(1);
        }
    }

    void Near(float actual, float expected, const char* message) {
        if (!std::isfinite(actual) || std::abs(actual - expected) > 0.0001f) {
            std::fprintf(stderr, "FAIL: %s (actual %.6f, expected %.6f)\n", message, actual, expected);
            std::exit(1);
        }
    }

    struct Fixture {
        UserState state;
        TriggerSystem triggers;
        FrameSteps steps;
        int physicsCalls = 0;
        int triggerUpdates = 0;
        int triggerEnters = 0;
        int effectsCalls = 0;
        int sceneCalls = 0;
        int renderAttempts = 0;
        int drawnFrames = 0;
        int audioCalls = 0;
        bool rendererReturnsEarly = false;
        bool winOnEnter = false;
        bool committedFlowStayedPlaying = true;
        std::vector<std::string> stages;

        Fixture() {
            AddTrigger();
            Require(state.gameFlow.Request(GameFlowCommand::Start), "fixture starts game");
            state.frame = FrameScheduler::BeginFrame(state.gameFlow, [] { return true; });
            steps.gameplay = [this](float) { RecordSimulationStage("gameplay"); };
            steps.events = [this](float) { RecordSimulationStage("events"); };
            steps.physics = [this](float) {
                RecordSimulationStage("physics");
                ++physicsCalls;
                // Simulates the actual post-physics PlayerController output.
                state.player.PublishMotion(40.0f, 0.0f, 0.0f, 0.0f, glm::vec3(5, 0, 0));
            };
            steps.postPhysics = [this](float) {
                RecordSimulationStage("postPhysics");
                ++triggerUpdates;
                triggers.Update(state.player.State().position);
            };
            steps.scene = [this](float) {
                stages.push_back("scene");
                ++sceneCalls;
                if (state.frame.runSimulationSystems)
                    committedFlowStayedPlaying &= state.gameFlow.State() == GameFlowState::Playing;
            };
            steps.animation = [this](float) { RecordSimulationStage("animation"); };
            steps.gameEffects = [this](float dt) {
                RecordSimulationStage("gameEffects");
                ++effectsCalls;
                state.hud.Update(dt, state.player.State(), state.frame);
                state.vehiclePresentation.Update(dt, state.player.State(), state.level, state.frame, 91, 23);
            };
            steps.presentation = [this](float) {
                stages.push_back("presentation");
                ++renderAttempts;
                if (rendererReturnsEarly) return;
                ++drawnFrames;
            };
            steps.audio = [this](float) { stages.push_back("audio"); ++audioCalls; };
        }

        void AddTrigger() {
            const auto id = triggers.AddSphereTrigger(glm::vec3(5, 0, 0), 1.0f, 2);
            triggers.SetTriggerCallbacks(id, [this] {
                ++triggerEnters;
                if (winOnEnter)
                    Require(state.gameFlow.Request(GameFlowCommand::Victory), "trigger queues victory");
            });
        }

        void RecordSimulationStage(const char* stage) {
            stages.push_back(stage);
            committedFlowStayedPlaying &= state.gameFlow.State() == GameFlowState::Playing;
        }

        void Execute() { FrameScheduler::Execute(state.frame, kDt, steps); }

        void NextFrame() {
            state.frame = FrameScheduler::BeginFrame(state.gameFlow, [] { return true; });
            stages.clear();
        }
    };

    void RendererEarlyReturnCannotSkipGame() {
        Fixture fixture;
        fixture.rendererReturnsEarly = true;
        fixture.Execute();
        Require(fixture.triggerUpdates == 1 && fixture.triggerEnters == 1,
            "trigger runs once even if renderer returns before drawing");
        Require(fixture.effectsCalls == 1 && fixture.physicsCalls == 1,
            "game effects run exactly once after physics");
        Near(fixture.state.hud.State().distanceMeters, 4.0f, "HUD uses post-physics velocity without a draw");
        Near(fixture.state.hud.State().lapTimeSeconds, kDt, "HUD clock advances without a draw");
        Near(fixture.state.vehiclePresentation.State().portalEffectTime, kDt, "effects clock advances without a draw");
        Near(fixture.state.vehiclePresentation.State().speedFactor, 0.5f, "vehicle response advances once");
        Require(fixture.renderAttempts == 1 && fixture.drawnFrames == 0 && fixture.audioCalls == 1,
            "render failure leaves frame continuation intact");
        const auto particleStates = fixture.triggers.ParticleStates();
        Require(particleStates.size() == 1 && particleStates[0].particleIndex == 2 && particleStates[0].active,
            "trigger particle output is produced without rendering");
        Require(fixture.stages == std::vector<std::string>{"gameplay", "events", "physics", "postPhysics",
            "scene", "animation", "gameEffects", "presentation", "audio"}, "production frame order");
    }

    void PresentationCanBeDisabled() {
        Fixture fixture;
        fixture.state.frame.runPresentation = false;
        fixture.Execute();
        Require(fixture.renderAttempts == 0 && fixture.drawnFrames == 0, "presentation stage is absent");
        Require(fixture.triggerUpdates == 1 && fixture.triggerEnters == 1 && fixture.effectsCalls == 1,
            "trigger and effects still execute exactly once");
        Near(fixture.state.hud.State().distanceMeters, 4.0f, "HUD accumulates when presentation is disabled");
        Near(fixture.state.vehiclePresentation.State().portalEffectTime, kDt, "effects advance when presentation is disabled");
        fixture.NextFrame();
        fixture.state.frame.runPresentation = false;
        fixture.Execute();
        Require(fixture.triggerUpdates == 2 && fixture.triggerEnters == 1 && fixture.effectsCalls == 2,
            "second frame evaluates triggers but does not duplicate enter callbacks");
        Near(fixture.state.hud.State().distanceMeters, 8.0f, "each headless simulation frame counts once");
    }

    void PauseSettingsAndTerminalStatesFreeze() {
        for (const auto command : {GameFlowCommand::Pause, GameFlowCommand::OpenSettings,
            GameFlowCommand::Victory, GameFlowCommand::GameOver}) {
            Fixture fixture;
            fixture.Execute();
            const auto oldHud = fixture.state.hud.State();
            const auto oldEffects = fixture.state.vehiclePresentation.State();
            Require(fixture.state.gameFlow.Request(command), "inactive flow request accepted");
            fixture.NextFrame();
            Require(!fixture.state.frame.runSimulationSystems, "inactive frame freezes simulation");
            fixture.Execute();
            Require(fixture.physicsCalls == 1 && fixture.triggerUpdates == 1 && fixture.effectsCalls == 1,
                "inactive frame never evaluates triggers or game effects");
            Near(fixture.state.hud.State().distanceMeters, oldHud.distanceMeters, "inactive HUD distance remains");
            Near(fixture.state.hud.State().lapTimeSeconds, oldHud.lapTimeSeconds, "inactive HUD time remains");
            Near(fixture.state.vehiclePresentation.State().portalEffectTime, oldEffects.portalEffectTime,
                "inactive game effect clock remains");
            Require(fixture.sceneCalls == 2 && fixture.renderAttempts == 2 && fixture.audioCalls == 2,
                "editor transforms, presentation and audio stay available");
            Require(fixture.stages == std::vector<std::string>{"scene", "presentation", "audio"},
                "inactive frame only runs permitted stages");
        }
    }

    void ReloadResetsSessionPreservesPreferences() {
        Fixture fixture;
        fixture.state.render.bloomEnabled = false;
        fixture.state.render.bloomExposure = 1.75f;
        fixture.state.editor.showEngineUi = true;
        fixture.state.editor.showEntityInspector = false;
        fixture.state.editor.activeParticleIndex = 4;
        fixture.state.capabilities.wireframeSupported = true;
        const auto rendererView = fixture.state.Renderer();
        fixture.Execute();
        Require(fixture.state.gameFlow.Request(GameFlowCommand::Restart), "restart accepted");
        int reloads = 0;
        fixture.state.frame = FrameScheduler::BeginFrame(fixture.state.gameFlow, [&] {
            ++reloads;
            fixture.triggers.ClearTriggers();
            fixture.state.ResetSession();
            fixture.AddTrigger();
            return true;
        });
        Require(reloads == 1 && fixture.state.frame.reloadHandled &&
            fixture.state.frame.flowState == GameFlowState::Playing && !fixture.state.frame.runSimulationSystems,
            "reload completion frame never simulates a partial scene");
        fixture.Execute();
        Require(fixture.triggerUpdates == 1 && fixture.effectsCalls == 1, "reload frame does not advance reset outputs");
        Near(rendererView.hud.distanceMeters, 0.0f, "existing renderer HUD view observes session reset");
        Near(rendererView.effects.portalEffectTime, 0.0f, "existing renderer effects view observes session reset");
        Require(fixture.triggers.Volumes().size() == 1 && !fixture.triggers.Volumes()[0].wasInside,
            "new trigger has clean crossing state");
        Require(!fixture.state.render.bloomEnabled && fixture.state.editor.showEngineUi &&
            !fixture.state.editor.showEntityInspector && fixture.state.capabilities.wireframeSupported,
            "render preferences, editor workspace and capabilities survive reset");
        Near(fixture.state.render.bloomExposure, 1.75f, "exposure preference survives reset");
        Require(fixture.state.editor.activeParticleIndex == -1, "stale scene selection is reset");
        fixture.NextFrame();
        fixture.Execute();
        Require(fixture.triggerEnters == 2, "new session trigger can enter afresh");
        Near(rendererView.hud.distanceMeters, 4.0f, "next frame starts fresh HUD accumulation");
    }

    void VictoryTriggerCannotTearFrame() {
        Fixture fixture;
        fixture.winOnEnter = true;
        fixture.Execute();
        Require(fixture.state.gameFlow.State() == GameFlowState::Playing &&
            fixture.state.gameFlow.PendingState() == GameFlowState::Victory, "trigger victory remains queued");
        Require(fixture.committedFlowStayedPlaying, "every simulation phase observes one committed flow");
        Require(fixture.physicsCalls == 1 && fixture.effectsCalls == 1 && fixture.sceneCalls == 1,
            "trigger victory does not cut later simulation phases");
        Near(fixture.state.hud.State().distanceMeters, 4.0f, "winning frame contributes final HUD statistics");
        Near(fixture.state.vehiclePresentation.State().portalEffectTime, kDt, "winning frame advances effects once");
        fixture.NextFrame();
        Require(fixture.state.frame.flowState == GameFlowState::Victory, "next boundary commits victory");
        fixture.Execute();
        Require(fixture.physicsCalls == 1 && fixture.triggerUpdates == 1 && fixture.effectsCalls == 1,
            "victory frame consistently stops every simulation stage");
        Near(fixture.state.hud.State().distanceMeters, 4.0f, "victory presentation cannot add statistics");
    }
}

int main() {
    const std::pair<const char*, void(*)()> tests[] = {
        {"renderer early return cannot skip gameplay outputs", RendererEarlyReturnCannotSkipGame},
        {"presentation can be disabled independently", PresentationCanBeDisabled},
        {"pause, settings and terminal flows freeze gameplay outputs", PauseSettingsAndTerminalStatesFreeze},
        {"reload resets session outputs and preserves preferences", ReloadResetsSessionPreservesPreferences},
        {"victory trigger cannot tear the committed frame", VictoryTriggerCannotTearFrame},
    };
    for (const auto& [name, test] : tests) {
        test();
        std::printf("PASS: %s\n", name);
    }
    std::printf("All %zu frame presentation regression groups passed.\n", std::size(tests));
}
