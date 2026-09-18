#pragma once

#include <cstdint>

namespace engine {

enum class GameFlowState { MainMenu, Playing, Paused, GameOver, Victory, Loading, LoadFailed };
enum class GameFlowCommand {
    Start, Pause, Resume, OpenSettings, CloseSettings,
    GameOver, Victory, Restart, ReturnToMainMenu
};

// Sole authority for session flow. UI and gameplay submit commands; Application
// consumes reload work and reports its result. Settings is an independent pause reason.
class GameFlowController {
public:
    GameFlowState State() const { return mState; }
    bool IsSettingsOpen() const { return mSettingsOpen; }
    bool CanSimulate() const { return mState == GameFlowState::Playing && !mSettingsOpen; }
    std::uint64_t Revision() const { return mRevision; }
    GameFlowState ReloadTarget() const { return mReloadTarget; }

    bool Request(GameFlowCommand command) {
        if (mState == GameFlowState::Loading) return false;
        switch (command) {
        case GameFlowCommand::Start:
            if (mState == GameFlowState::LoadFailed) return BeginReload(GameFlowState::Playing);
            if (mState != GameFlowState::MainMenu || mSettingsOpen) return false;
            mState = GameFlowState::Playing;
            break;
        case GameFlowCommand::Pause:
            if (mState != GameFlowState::Playing) return false;
            mState = GameFlowState::Paused;
            break;
        case GameFlowCommand::Resume:
            if (mState != GameFlowState::Paused) return false;
            mState = GameFlowState::Playing;
            break;
        case GameFlowCommand::OpenSettings:
            if (mSettingsOpen || (mState != GameFlowState::MainMenu &&
                mState != GameFlowState::Playing && mState != GameFlowState::Paused)) return false;
            mSettingsOpen = true;
            break;
        case GameFlowCommand::CloseSettings:
            if (!mSettingsOpen) return false;
            mSettingsOpen = false;
            break;
        case GameFlowCommand::GameOver:
        case GameFlowCommand::Victory:
            if (mState != GameFlowState::Playing && mState != GameFlowState::Paused) return false;
            mState = command == GameFlowCommand::Victory ? GameFlowState::Victory : GameFlowState::GameOver;
            mSettingsOpen = false;
            break;
        case GameFlowCommand::Restart:
            if (mState == GameFlowState::MainMenu) return false;
            return BeginReload(GameFlowState::Playing);
        case GameFlowCommand::ReturnToMainMenu:
            if (mState == GameFlowState::MainMenu) return false;
            return BeginReload(GameFlowState::MainMenu);
        }
        ++mRevision;
        return true;
    }

    // At most one consumer may start the work associated with a Loading transition.
    bool TakeReloadRequest() {
        if (mState != GameFlowState::Loading || mReloadInFlight) return false;
        mReloadInFlight = true;
        return true;
    }

    bool CompleteReload(bool success) {
        if (mState != GameFlowState::Loading || !mReloadInFlight) return false;
        mState = success ? mReloadTarget : GameFlowState::LoadFailed;
        mReloadInFlight = false;
        ++mRevision;
        return true;
    }

private:
    bool BeginReload(GameFlowState target) {
        mReloadTarget = target;
        mState = GameFlowState::Loading;
        mSettingsOpen = false;
        mReloadInFlight = false;
        ++mRevision;
        return true;
    }

    GameFlowState mState = GameFlowState::MainMenu;
    GameFlowState mReloadTarget = GameFlowState::MainMenu;
    bool mSettingsOpen = false;
    bool mReloadInFlight = false;
    std::uint64_t mRevision = 0;
};

} // namespace engine
