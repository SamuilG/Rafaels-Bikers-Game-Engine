#include "Runtime/UserState/GameFlowController.hpp"

#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <string>

using namespace engine;

namespace {
    void Require(bool condition, const std::string& message) {
        if (!condition) {
            std::fprintf(stderr, "FAIL: %s\n", message.c_str());
            std::exit(1);
        }
    }

    void Expect(const GameFlowController& flow, GameFlowState state, bool settings, bool simulates) {
        Require(flow.State() == state, "expected authoritative state");
        Require(flow.IsSettingsOpen() == settings, "expected settings overlay state");
        Require(flow.CanSimulate() == simulates, "expected simulation gate");
    }

    void Reject(GameFlowController& flow, std::initializer_list<GameFlowCommand> commands) {
        const auto state = flow.State();
        const auto settings = flow.IsSettingsOpen();
        const auto simulates = flow.CanSimulate();
        const auto revision = flow.Revision();
        const auto target = flow.ReloadTarget();
        for (const auto command : commands) {
            Require(!flow.Request(command), "invalid or duplicate request is rejected");
            Expect(flow, state, settings, simulates);
            Require(flow.Revision() == revision && flow.ReloadTarget() == target,
                "rejected request leaves revision and reload destination unchanged");
        }
    }

    void Accept(GameFlowController& flow, GameFlowCommand command) {
        const auto revision = flow.Revision();
        Require(flow.Request(command), "valid request is accepted");
        Require(flow.Revision() > revision, "accepted state change advances UI revision");
    }

    void Finish(GameFlowController& flow, bool success, GameFlowState expected) {
        Require(flow.State() == GameFlowState::Loading && !flow.CanSimulate(), "reload keeps simulation stopped");
        Require(!flow.CompleteReload(success), "completion before host takes request is rejected");
        Require(flow.TakeReloadRequest(), "host takes one pending request");
        Require(!flow.TakeReloadRequest(), "host cannot consume the same request twice");
        Require(flow.CompleteReload(success), "host reports one completion");
        Expect(flow, expected, false, expected == GameFlowState::Playing);
        const auto completedRevision = flow.Revision();
        Require(!flow.CompleteReload(success) && !flow.TakeReloadRequest(), "completed reload cannot replay");
        Require(flow.Revision() == completedRevision, "duplicate completion is not a UI update");
    }

    void TestInitialAndSettings() {
        GameFlowController flow;
        Expect(flow, GameFlowState::MainMenu, false, false);
        Require(!flow.TakeReloadRequest() && !flow.CompleteReload(true), "initial state has no reload request");
        Reject(flow, {GameFlowCommand::Pause, GameFlowCommand::Resume, GameFlowCommand::CloseSettings,
            GameFlowCommand::GameOver, GameFlowCommand::Victory, GameFlowCommand::Restart,
            GameFlowCommand::ReturnToMainMenu});
        Accept(flow, GameFlowCommand::OpenSettings);
        Expect(flow, GameFlowState::MainMenu, true, false);
        Reject(flow, {GameFlowCommand::OpenSettings, GameFlowCommand::Start});
        Accept(flow, GameFlowCommand::CloseSettings);
        Expect(flow, GameFlowState::MainMenu, false, false);
        Accept(flow, GameFlowCommand::Start);
        Expect(flow, GameFlowState::Playing, false, true);
        Require(!flow.TakeReloadRequest(), "first start uses the already initialized scene");
        Reject(flow, {GameFlowCommand::Start, GameFlowCommand::Resume});
        std::puts("PASS initial state, main-menu settings and first start");
    }

    void TestIndependentPauseReasons() {
        GameFlowController flow;
        Accept(flow, GameFlowCommand::Start);
        Accept(flow, GameFlowCommand::OpenSettings);
        Expect(flow, GameFlowState::Playing, true, false);
        Accept(flow, GameFlowCommand::Pause);
        Expect(flow, GameFlowState::Paused, true, false);
        Accept(flow, GameFlowCommand::CloseSettings);
        Expect(flow, GameFlowState::Paused, false, false);
        Accept(flow, GameFlowCommand::OpenSettings);
        Accept(flow, GameFlowCommand::Resume);
        Expect(flow, GameFlowState::Playing, true, false);
        Accept(flow, GameFlowCommand::CloseSettings);
        Expect(flow, GameFlowState::Playing, false, true);
        Accept(flow, GameFlowCommand::Pause);
        Reject(flow, {GameFlowCommand::Pause, GameFlowCommand::Start});
        Accept(flow, GameFlowCommand::Resume);
        Expect(flow, GameFlowState::Playing, false, true);
        std::puts("PASS pause and settings independently stop simulation");
    }

    void TestResultsAndReload() {
        for (const auto result : {GameFlowCommand::GameOver, GameFlowCommand::Victory}) {
            GameFlowController flow;
            Accept(flow, GameFlowCommand::Start);
            Accept(flow, GameFlowCommand::OpenSettings);
            Accept(flow, result);
            Expect(flow, result == GameFlowCommand::Victory ? GameFlowState::Victory : GameFlowState::GameOver,
                false, false);
            Reject(flow, {GameFlowCommand::Start, GameFlowCommand::Pause, GameFlowCommand::Resume, GameFlowCommand::OpenSettings,
                GameFlowCommand::CloseSettings, GameFlowCommand::GameOver, GameFlowCommand::Victory});
            Accept(flow, GameFlowCommand::Restart);
            Expect(flow, GameFlowState::Loading, false, false);
            Require(flow.ReloadTarget() == GameFlowState::Playing, "restart destination is Playing");
            Reject(flow, {GameFlowCommand::Start, GameFlowCommand::Pause, GameFlowCommand::Resume,
                GameFlowCommand::OpenSettings, GameFlowCommand::CloseSettings, GameFlowCommand::GameOver,
                GameFlowCommand::Victory, GameFlowCommand::Restart, GameFlowCommand::ReturnToMainMenu});
            Finish(flow, true, GameFlowState::Playing);
        }
        std::puts("PASS both result states, illegal transitions and one-shot reload completion");
    }

    void TestFailureAndRetry() {
        GameFlowController flow;
        Accept(flow, GameFlowCommand::Start);
        Accept(flow, GameFlowCommand::Restart);
        Finish(flow, false, GameFlowState::LoadFailed);
        Reject(flow, {GameFlowCommand::Pause, GameFlowCommand::Resume, GameFlowCommand::OpenSettings,
            GameFlowCommand::CloseSettings, GameFlowCommand::GameOver, GameFlowCommand::Victory});
        Accept(flow, GameFlowCommand::Start);
        Expect(flow, GameFlowState::Loading, false, false);
        Require(flow.ReloadTarget() == GameFlowState::Playing, "Start from failure retries the scene");
        Finish(flow, false, GameFlowState::LoadFailed);
        Accept(flow, GameFlowCommand::Restart);
        Finish(flow, true, GameFlowState::Playing);
        std::puts("PASS repeated load failures remain stopped and retry can succeed");
    }

    void TestReturnToMainMenu() {
        GameFlowController flow;
        Accept(flow, GameFlowCommand::Start);
        Accept(flow, GameFlowCommand::Pause);
        Accept(flow, GameFlowCommand::OpenSettings);
        Accept(flow, GameFlowCommand::ReturnToMainMenu);
        Expect(flow, GameFlowState::Loading, false, false);
        Require(flow.ReloadTarget() == GameFlowState::MainMenu, "return destination is MainMenu");
        Finish(flow, true, GameFlowState::MainMenu);
        Accept(flow, GameFlowCommand::Start);
        Require(!flow.TakeReloadRequest(), "start after successful menu reload does not reload again");
        Accept(flow, GameFlowCommand::ReturnToMainMenu);
        Finish(flow, false, GameFlowState::LoadFailed);
        Accept(flow, GameFlowCommand::ReturnToMainMenu);
        Require(flow.ReloadTarget() == GameFlowState::MainMenu, "failed return can retry its menu destination");
        Finish(flow, true, GameFlowState::MainMenu);
        std::puts("PASS menu reload clears overlays, succeeds or fails safely, and supports retry");
    }
}

int main() {
    TestInitialAndSettings();
    TestIndependentPauseReasons();
    TestResultsAndReload();
    TestFailureAndRetry();
    TestReturnToMainMenu();
    std::puts("PASS all real GameFlowController tests (standard C++ only)");
}
