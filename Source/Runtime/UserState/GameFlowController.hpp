#pragma once

#include <cstdint>

namespace engine {

enum class GameFlowState { MainMenu, Playing, Paused, GameOver, Victory, Loading, LoadFailed };
enum class GameFlowCommand {
    Start, Pause, Resume, OpenSettings, CloseSettings,
    GameOver, Victory, Restart, ReturnToMainMenu
};

// Sole authority for session flow. UI and gameplay enqueue commands; Application
// commits them at the next frame boundary before choosing that frame's execution.
// Settings is an independent pause reason. Reload completion belongs to the same
// application boundary, before any simulation or presentation work begins.
class GameFlowController {
public:
    GameFlowState State() const { return mCommitted.state; }
    bool IsSettingsOpen() const { return mCommitted.settingsOpen; }
    bool CanSimulate() const { return State() == GameFlowState::Playing && !IsSettingsOpen(); }
    std::uint64_t Revision() const { return mRevision; }
    GameFlowState ReloadTarget() const { return mCommitted.reloadTarget; }

    // The projected result is for command validation and UI asset preparation,
    // never for deciding whether a system should run in the current frame.
    GameFlowState PendingState() const { return mPending.state; }
    bool PendingSettingsOpen() const { return mPending.settingsOpen; }
    GameFlowState PendingReloadTarget() const { return mPending.reloadTarget; }
    bool HasPendingRequests() const { return mHasPendingRequests; }

    bool Request(GameFlowCommand command) {
        if (!ApplyCommand(mPending, command)) return false;
        mHasPendingRequests = true;
        return true;
    }

    // Publish one accepted batch atomically. Even a batch such as Pause+Resume
    // advances one generation; an empty/repeated commit is a strict no-op.
    // Production callers must commit only at Application's frame boundary.
    bool CommitPendingRequests() {
        if (!mHasPendingRequests) return false;
        mCommitted = mPending;
        mHasPendingRequests = false;
        mReloadInFlight = false;
        ++mRevision;
        return true;
    }

    // At most one consumer may start the work associated with a committed Loading
    // transition. A queued reload cannot be consumed before the frame boundary.
    bool TakeReloadRequest() {
        if (State() != GameFlowState::Loading || mReloadInFlight) return false;
        mReloadInFlight = true;
        return true;
    }

    bool CompleteReload(bool success) {
        if (State() != GameFlowState::Loading || !mReloadInFlight) return false;
        mCommitted.state = success ? mCommitted.reloadTarget : GameFlowState::LoadFailed;
        mPending = mCommitted;
        mReloadInFlight = false;
        ++mRevision;
        return true;
    }

private:
    struct FlowSnapshot {
        GameFlowState state = GameFlowState::MainMenu;
        GameFlowState reloadTarget = GameFlowState::MainMenu;
        bool settingsOpen = false;
    };

    static bool ApplyCommand(FlowSnapshot& next, GameFlowCommand command) {
        if (next.state == GameFlowState::Loading) return false;
        switch (command) {
        case GameFlowCommand::Start:
            if (next.state == GameFlowState::LoadFailed) return BeginReload(next, GameFlowState::Playing);
            if (next.state != GameFlowState::MainMenu || next.settingsOpen) return false;
            next.state = GameFlowState::Playing;
            break;
        case GameFlowCommand::Pause:
            if (next.state != GameFlowState::Playing) return false;
            next.state = GameFlowState::Paused;
            break;
        case GameFlowCommand::Resume:
            if (next.state != GameFlowState::Paused) return false;
            next.state = GameFlowState::Playing;
            break;
        case GameFlowCommand::OpenSettings:
            if (next.settingsOpen || (next.state != GameFlowState::MainMenu &&
                next.state != GameFlowState::Playing && next.state != GameFlowState::Paused)) return false;
            next.settingsOpen = true;
            break;
        case GameFlowCommand::CloseSettings:
            if (!next.settingsOpen) return false;
            next.settingsOpen = false;
            break;
        case GameFlowCommand::GameOver:
        case GameFlowCommand::Victory:
            if (next.state != GameFlowState::Playing && next.state != GameFlowState::Paused) return false;
            next.state = command == GameFlowCommand::Victory ? GameFlowState::Victory : GameFlowState::GameOver;
            next.settingsOpen = false;
            break;
        case GameFlowCommand::Restart:
            if (next.state == GameFlowState::MainMenu) return false;
            return BeginReload(next, GameFlowState::Playing);
        case GameFlowCommand::ReturnToMainMenu:
            if (next.state == GameFlowState::MainMenu) return false;
            return BeginReload(next, GameFlowState::MainMenu);
        default:
            return false;
        }
        return true;
    }

    static bool BeginReload(FlowSnapshot& next, GameFlowState target) {
        next.reloadTarget = target;
        next.state = GameFlowState::Loading;
        next.settingsOpen = false;
        return true;
    }

    FlowSnapshot mCommitted;
    FlowSnapshot mPending;
    bool mHasPendingRequests = false;
    bool mReloadInFlight = false;
    std::uint64_t mRevision = 0;
};

} // namespace engine
