#pragma once

#include <string>
#include <utility>

namespace engine {

// Editor transport is independent of the game's own menu/pause flow. Commands
// requested by UI are consumed only at Application's next frame boundary.
class EditorPlayController {
public:
    enum class State { Editing, Playing, Paused };
    enum class Command { None, Play, TogglePause, Stop };

    void Configure(bool available) { mAvailable = available; }
    bool Available() const { return mAvailable; }
    bool IsPlaying() const { return mState != State::Editing; }
    bool IsPaused() const { return mState == State::Paused; }
    bool IsSimulating() const { return mState == State::Playing; }
    Command PendingCommand() const { return mPending; }
    const std::string& Error() const { return mError; }

    bool RequestPlay() {
        if (!mAvailable || IsPlaying() || mPending != Command::None) return false;
        mPending = Command::Play;
        return true;
    }
    bool RequestPause() {
        if (!IsPlaying() || mPending != Command::None) return false;
        mPending = Command::TogglePause;
        return true;
    }
    bool RequestStop() {
        if (!IsPlaying() && mPending != Command::Play) return false;
        // Stop always wins, including a Play requested in this UI frame.
        mPending = Command::Stop;
        return true;
    }

    template<class Start, class Stop>
    bool Commit(Start&& start, Stop&& stop) {
        const Command command = std::exchange(mPending, Command::None);
        if (command == Command::None) return false;
        if (command == Command::Play) {
            mError = start();
            if (mError.empty()) mState = State::Playing;
        } else if (command == Command::TogglePause) {
            if (IsPlaying()) mState = IsPaused() ? State::Playing : State::Paused;
        } else if (command == Command::Stop) {
            if (IsPlaying()) stop();
            mState = State::Editing;
            mError.clear();
        }
        return true;
    }

private:
    bool mAvailable = false;
    State mState = State::Editing;
    Command mPending = Command::None;
    std::string mError;
};

} // namespace engine
