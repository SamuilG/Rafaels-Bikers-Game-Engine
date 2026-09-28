#include "Runtime/Core/EditorPlayController.hpp"
#include "Runtime/Core/FrameScheduler.hpp"
#include <iostream>
#include <stdexcept>
#include <string>

using namespace engine;

static void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

int main() {
    try {
        EditorPlayController transport;
        int starts = 0, stops = 0;
        auto start = [&]() -> std::string { ++starts; return {}; };
        auto stop = [&]() { ++stops; };
        Require(!transport.RequestPlay(), "Blank host must reject Play");
        transport.Configure(true);
        Require(transport.RequestPlay(), "Configured editor must accept Play");
        Require(transport.PendingCommand() == EditorPlayController::Command::Play, "Loading feedback must be visible before the blocking frame boundary");
        Require(!transport.IsPlaying() && starts == 0, "Play must wait for frame boundary");
        Require(!transport.RequestPlay(), "Duplicate pending Play must be rejected");
        transport.Commit(start, stop);
        Require(transport.PendingCommand() == EditorPlayController::Command::None, "Completed load must clear loading feedback");
        Require(transport.IsSimulating() && starts == 1, "Boundary must start exactly one session");
        transport.Commit(start, stop);
        Require(starts == 1, "Empty boundary must not start a second session");
        std::cout << "PASS deferred Play and blank-host gate\n";

        GameFlowController flow;
        flow.Request(GameFlowCommand::Start);
        auto frame = FrameScheduler::BeginFrame(flow, []() { return true; }, !transport.IsSimulating());
        Require(frame.runSimulationSystems, "Playing editor must permit the game schedule");
        transport.RequestPause();
        Require(transport.IsSimulating(), "Pause must be deferred");
        transport.Commit(start, stop);
        frame = FrameScheduler::BeginFrame(flow, []() { return true; }, !transport.IsSimulating());
        Require(!frame.runSimulationSystems && frame.runPresentation, "Editor pause must preserve presentation only");
        Require(flow.State() == GameFlowState::Playing, "Editor pause must not change game flow");
        Require(transport.IsPlaying() && transport.IsPaused(), "Paused transport still owns a play session");
        std::cout << "PASS independent editor pause and frame gating\n";

        flow.Request(GameFlowCommand::Pause);
        transport.RequestPause();
        transport.Commit(start, stop);
        frame = FrameScheduler::BeginFrame(flow, []() { return true; }, !transport.IsSimulating());
        Require(transport.IsSimulating() && !frame.runSimulationSystems,
            "Resuming editor must not dismiss an in-game pause");
        flow.Request(GameFlowCommand::Resume);
        frame = FrameScheduler::BeginFrame(flow, []() { return true; }, !transport.IsSimulating());
        Require(frame.runSimulationSystems, "Both pause reasons cleared must allow simulation");
        std::cout << "PASS in-game pause survives editor resume\n";

        transport.RequestPause();
        Require(transport.RequestStop(), "Stop must override a pending pause");
        transport.Commit(start, stop);
        Require(!transport.IsPlaying() && stops == 1, "Stop must restore exactly once");
        Require(!transport.RequestStop(), "Stopped session cannot be stopped twice");
        transport.RequestPlay();
        transport.RequestStop();
        transport.Commit(start, stop);
        Require(starts == 1 && stops == 1 && !transport.IsPlaying(), "Play then Stop must not allocate a world");
        std::cout << "PASS Stop precedence and duplicate protection\n";

        transport.RequestPlay();
        transport.Commit([]() -> std::string { return "Scene load failed"; }, stop);
        Require(!transport.IsPlaying() && transport.Error() == "Scene load failed",
            "Failed Play must stay in editing and expose the failure");
        transport.RequestPlay();
        transport.Commit(start, stop);
        Require(transport.IsPlaying() && transport.Error().empty(), "Play must support retry after failure");
        transport.RequestStop();
        transport.Commit(start, stop);
        std::cout << "PASS failed load and subsequent retry\n";

        int presentation = 0, physics = 0, scene = 0;
        FrameSteps steps;
        steps.physics = [&](float) { ++physics; };
        steps.scene = [&](float) { ++scene; };
        steps.presentation = [&](float) { ++presentation; };
        frame = FrameScheduler::BeginFrame(flow, []() { return true; }, true);
        FrameScheduler::Execute(frame, 0.016f, steps);
        Require(physics == 0 && scene == 1 && presentation == 1,
            "Stopped editor must continue transform propagation and viewport rendering");
        std::cout << "PASS stopped editor keeps scene and presentation alive\n";
        std::cout << "6 editor play regression groups passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
