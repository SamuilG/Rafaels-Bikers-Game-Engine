#include "Runtime/Gameplay/HudController.hpp"
#include "Runtime/UI/GameHudBindings.hpp"
#include "Runtime/UserState/PlayerController.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <string>
#include <type_traits>
#include <utility>

using namespace engine;

static_assert(std::is_same_v<decltype(std::declval<HudController&>().State()), const HudData&>);

namespace {
    void Require(bool condition, const char* message) {
        if (!condition) {
            std::fprintf(stderr, "FAIL: %s\n", message);
            std::exit(1);
        }
    }

    void Near(float actual, float expected, const char* message, float tolerance = 0.0001f) {
        if (!std::isfinite(actual) || std::abs(actual - expected) > tolerance) {
            std::fprintf(stderr, "FAIL: %s (actual %.8f, expected %.8f)\n", message, actual, expected);
            std::exit(1);
        }
    }

    FrameExecution PlayingFrame() {
        FrameExecution frame;
        frame.flowState = GameFlowState::Playing;
        frame.runGameplay = true;
        frame.runSimulationSystems = true;
        return frame;
    }

    template<typename T>
    const T& Bound(const UIDataContext& context, const char* key) {
        const auto* value = context.GetValue(key);
        Require(value != nullptr, key);
        const auto* typed = std::get_if<T>(&value->data);
        Require(typed != nullptr, key);
        return *typed;
    }

    void UnitsAndDemoData() {
        PlayerController player;
        HudController hud;
        player.PublishMotion(10.0f, 0.0f, 0.0f, 0.0f);
        hud.Update(2.0f, player.State(), PlayingFrame());
        Near(hud.State().speedKmh, 36.0f, "10 m/s is 36 km/h");
        Near(hud.State().speedMph, 22.369356f, "mph uses the same converted speed");
        Near(hud.State().distanceMeters, 20.0f, "distance integrates m/s without dividing by 3.6");
        Near(hud.State().lapTimeSeconds, 2.0f, "timer uses simulation seconds");
        Near(hud.State().energy, 0.4f, "demo energy uses km/h");
        Near(hud.State().stamina, hud.State().energy, "demo stamina mirrors energy");
        Require(!hud.State().isLowEnergy && hud.State().currentLap == 1, "normal energy and demo lap");
        player.PublishMotion(12.5f, 0.0f, 0.0f, 0.0f);
        hud.Update(0.0f, player.State(), PlayingFrame());
        Require(hud.State().isLowEnergy, "45 km/h is low-energy boundary");
        Near(hud.State().warningColor.y, 0.26f, "low-energy warning color");
        player.PublishMotion(20.0f, 0.0f, 0.0f, 0.0f);
        hud.Update(0.0f, player.State(), PlayingFrame());
        Near(hud.State().energy, 0.0f, "demo energy clamps to zero");
    }

    void HeadlessSimulation() {
        PlayerController player;
        HudController hud;
        player.PublishMotion(12.0f, 0.0f, 0.0f, 0.0f);
        auto frame = PlayingFrame();
        frame.runPresentation = false;
        // No UI manager, draw calls, swapchain, or renderer are constructed.
        for (int i = 0; i < 600; ++i) hud.Update(1.0f / 60.0f, player.State(), frame);
        Near(hud.State().distanceMeters, 120.0f, "headless simulation retains distance", 0.001f);
        Near(hud.State().lapTimeSeconds, 10.0f, "headless simulation retains time");
        Require(hud.State().lapTime == "00:10.000", "headless formatted time");
    }

    void InactiveFramesFreeze() {
        PlayerController player;
        HudController hud;
        player.PublishMotion(8.0f, 0.0f, 0.0f, 0.0f);
        hud.Update(3.0f, player.State(), PlayingFrame());
        const HudData before = hud.State();
        player.PublishMotion(0.0f, 0.0f, 0.0f, 0.0f);
        for (const auto state : {GameFlowState::MainMenu, GameFlowState::Playing,
            GameFlowState::Paused, GameFlowState::GameOver, GameFlowState::Victory,
            GameFlowState::Loading, GameFlowState::LoadFailed}) {
            FrameExecution frame;
            frame.flowState = state;
            hud.Update(100.0f, player.State(), frame);
            Near(hud.State().distanceMeters, before.distanceMeters, "inactive frame preserves distance");
            Near(hud.State().lapTimeSeconds, before.lapTimeSeconds, "inactive frame preserves time");
            Near(hud.State().speedKmh, before.speedKmh, "inactive frame preserves snapshot");
            Require(hud.State().lapTime == before.lapTime, "inactive frame preserves formatted time");
        }
        auto settings = PlayingFrame();
        settings.settingsOpen = true;
        settings.runSimulationSystems = false;
        hud.Update(100.0f, player.State(), settings);
        auto reloaded = PlayingFrame();
        reloaded.reloadHandled = true;
        reloaded.runSimulationSystems = false;
        hud.Update(100.0f, player.State(), reloaded);
        Near(hud.State().distanceMeters, before.distanceMeters, "settings and reload completion do not tick");
    }

    void CommittedFrameWins() {
        GameFlowController flow;
        Require(flow.Request(GameFlowCommand::Start), "start request accepted");
        flow.CommitPendingRequests();
        const auto frame = FrameExecution::Plan(flow, false);
        PlayerController player;
        player.PublishMotion(6.0f, 0.0f, 0.0f, 0.0f);
        HudController hud;
        Require(flow.Request(GameFlowCommand::Victory), "victory queued during frame");
        hud.Update(0.5f, player.State(), frame);
        Near(hud.State().distanceMeters, 3.0f, "victory request does not cut the current simulation frame");
        flow.CommitPendingRequests();
        hud.Update(50.0f, player.State(), FrameExecution::Plan(flow, false));
        Near(hud.State().distanceMeters, 3.0f, "committed victory freezes subsequent frames");
    }

    void SessionReset() {
        PlayerController player;
        HudController hud;
        const auto& stableView = hud.State();
        player.PublishMotion(20.0f, 0.0f, 0.0f, 0.0f);
        hud.Update(70.0f, player.State(), PlayingFrame());
        hud.ResetForNewRun();
        Near(stableView.distanceMeters, 0.0f, "reset clears distance through existing const view");
        Near(stableView.lapTimeSeconds, 0.0f, "reset clears timer");
        Near(stableView.speedKmh, 0.0f, "reset clears speed");
        Near(stableView.energy, 1.0f, "reset restores demo energy");
        Require(stableView.lapTime == "00:00.000" && stableView.bestLapTime == "00:00.000",
            "reset clears both formatted clocks");
        Require(!stableView.isLowEnergy && stableView.currentLap == 1, "reset restores warning and lap");
        player.PublishMotion(1.0f, 0.0f, 0.0f, 0.0f);
        hud.Update(1.0f, player.State(), PlayingFrame());
        Near(stableView.distanceMeters, 1.0f, "new run starts with fresh distance accumulator");
        Near(stableView.lapTimeSeconds, 1.0f, "new run starts with fresh clock accumulator");
    }

    void ClockFormatting() {
        HudController hud;
        hud.Update(59.9996f, PlayerState{}, PlayingFrame());
        Require(hud.State().lapTime == "01:00.000", "millisecond rounding carries into minutes");
        hud.Update(1.25f, PlayerState{}, PlayingFrame());
        Require(hud.State().lapTime == "01:01.250", "formatted time includes minutes and milliseconds");
        Require(hud.State().bestLapTime == hud.State().lapTime, "demo best-time placeholder matches elapsed time");
        Near(hud.State().bestLapTimeSeconds, hud.State().lapTimeSeconds, "numeric best-time placeholder matches");
    }

    void ActualUiBindings() {
        HudController hud;
        PlayerController player;
        player.PublishMotion(10.0f, 0.0f, 0.0f, 0.0f);
        hud.Update(2.0f, player.State(), PlayingFrame());
        UIDataContext context;
        context.SetFloat("ui.hudOpacity", 0.75f);
        BindHudData(context, hud.State());
        Near(Bound<float>(context, "bike.speedKmh"), 36.0f, "UI binds km/h without a second conversion");
        Near(Bound<float>(context, "bike.speedMph"), 22.369356f, "UI binds mph");
        Near(Bound<float>(context, "bike.distance"), 20.0f, "UI binds metres without conversion");
        Near(Bound<float>(context, "bike.energy"), hud.State().energy, "UI binds demo energy");
        Near(Bound<float>(context, "bike.stamina"), hud.State().stamina, "UI binds demo stamina");
        Require(Bound<bool>(context, "bike.isLowEnergy") == hud.State().isLowEnergy &&
            Bound<bool>(context, "bike.lowStamina") == hud.State().isLowEnergy, "warning aliases agree");
        Near(Bound<glm::vec4>(context, "bike.warningColor").w, hud.State().warningColor.w, "UI binds warning color");
        Near(Bound<float>(context, "race.lapTimeSeconds"), 2.0f, "UI binds elapsed seconds");
        Near(Bound<float>(context, "race.bestLapTimeSeconds"), 2.0f, "UI binds best placeholder seconds");
        Require(Bound<int>(context, "race.currentLap") == 1, "UI binds current lap");
        Require(Bound<std::string>(context, "race.lapTime") == "00:02.000" &&
            Bound<std::string>(context, "race.bestLapTime") == "00:02.000", "UI uses prepared text");
        const auto revision = context.GetRevision("bike.distance");
        for (int i = 0; i < 120; ++i) BindHudData(context, hud.State());
        Near(hud.State().distanceMeters, 20.0f, "repeated UI refresh never changes gameplay statistics");
        Require(context.GetRevision("bike.distance") == revision, "unchanged HUD does not churn bindings");
        Near(Bound<float>(context, "ui.hudOpacity"), 0.75f, "HUD binding preserves UI preferences");
    }

    void InvalidTimeDoesNotCorrupt() {
        HudController hud;
        PlayerState player;
        player.bikeSpeed = 5.0f;
        hud.Update(1.0f, player, PlayingFrame());
        for (float dt : {0.0f, -1.0f, std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()})
            hud.Update(dt, player, PlayingFrame());
        Near(hud.State().distanceMeters, 5.0f, "invalid or non-positive time cannot corrupt distance");
        Near(hud.State().lapTimeSeconds, 1.0f, "invalid or non-positive time cannot corrupt clock");
        player.bikeSpeed = std::numeric_limits<float>::quiet_NaN();
        hud.Update(1.0f, player, PlayingFrame());
        Near(hud.State().speedKmh, 0.0f, "invalid motion feedback cannot poison HUD");
        Near(hud.State().distanceMeters, 5.0f, "invalid motion feedback does not add distance");
    }
}

int main() {
    const std::pair<const char*, void(*)()> tests[] = {
        {"units and demo presentation data", UnitsAndDemoData},
        {"headless simulation updates HUD", HeadlessSimulation},
        {"inactive frames freeze HUD", InactiveFramesFreeze},
        {"committed frame controls HUD update", CommittedFrameWins},
        {"session reset clears accumulators", SessionReset},
        {"clock formatting and demo placeholders", ClockFormatting},
        {"actual UI bindings are projection only", ActualUiBindings},
        {"invalid time and motion remain finite", InvalidTimeDoesNotCorrupt},
    };
    for (const auto& [name, test] : tests) {
        test();
        std::printf("PASS: %s\n", name);
    }
    std::printf("All %zu HUD regression groups passed.\n", std::size(tests));
}
