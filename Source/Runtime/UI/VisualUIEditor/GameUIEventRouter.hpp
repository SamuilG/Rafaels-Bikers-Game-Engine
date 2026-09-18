#pragma once

#include <string>
#include "../../UserState/GameFlowController.hpp"

namespace engine {

    class AudioSystem;
    class RuntimeUiController;
    class UIManager;
    struct UserState;

    // 运行时 UI 事件到游戏逻辑的路由层。
    // UIManager 只负责在按钮/控件交互后抛出事件名，
    // 此处提交 GameFlowController 命令、呈现结果，并处理应用退出。
    class GameUIEventRouter {
    public:
        // router 只持有外部系统的引用，不拥有它们的生命周期。
        GameUIEventRouter(RuntimeUiController& runtimeUiController, UserState& state, bool& appRunning);
        void SetAudioSystem(AudioSystem* audioSystem);

        // 把当前支持的事件名统一注册到 UIManager。
        void Bind(UIManager& uiManager);
        // Present the authoritative flow state; unchanged revisions do no work.
        bool SyncGameFlowUi();

    private:
        struct SettingsState {
            float appliedMasterVolume = 1.0f;
            float pendingMasterVolume = 1.0f;
            bool appliedShowHints = true;
            bool pendingShowHints = true;
            int appliedResolutionIndex = 0;
            int pendingResolutionIndex = 0;
            bool appliedFullscreen = false;
            bool pendingFullscreen = false;
        };

        // 主菜单里的 StartGame / MainMenu.StartGame 都会落到这里。
        void HandleStartGame(const std::string& eventName);
        // 进入暂停流程并显示 PauseMenu。
        void HandlePauseGame(const std::string& eventName);
        // Open the settings overlay without replacing the underlying flow.
        void HandleOpenSettings(const std::string& eventName);
        // Remove only the settings overlay and its independent pause reason.
        void HandleCloseSettings(const std::string& eventName);
        // 使用引擎现有 appRunning 退出机制关闭程序。
        void HandleQuitGame(const std::string& eventName);
        // Queue a reload to the main menu; Application reports completion.
        void HandleBackToMainMenu(const std::string& eventName);
        // 关闭暂停菜单并恢复 HUD 显示。
        void HandleResumeGame(const std::string& eventName);
        // 切换到 GameOver 状态，隐藏所有游戏中界面并显示结算屏幕。
        void HandleShowGameOver(const std::string& eventName);
        void HandleShowVictory(const std::string& eventName);
        void HandleMenuBack(const std::string& eventName);
        // Enter Loading; gameplay resumes only after successful host reload.
        void HandleRestartGame(const std::string& eventName);
        // 以编辑器模式启动游戏（开启引擎 UI）。
        void HandleOpenEditor(const std::string& eventName);
        // 调试按钮事件，方便验证整条运行时点击链路。
        void HandleTestButton(const std::string& eventName);
        void HandleApplySettings(const std::string& eventName);
        void HandleResetSettings(const std::string& eventName);
        void HandleVolumeChanged(const std::string& eventName);
        void HandleToggleChanged(const std::string& eventName);
        void HandleResolutionPrev(const std::string& eventName);
        void HandleResolutionNext(const std::string& eventName);
        void HandleDisplayModeToggle(const std::string& eventName);
        void RefreshPendingSettingsFromGame();
        void CapturePendingSettingsFromUi();
        void SyncSettingsUi();
        void SyncHudHintUi();
        bool RequestFlow(GameFlowCommand command, const std::string& eventName);
        bool PrepareFlowScreens(GameFlowState flow, bool settingsOpen);
        int FindResolutionIndex(int width, int height) const;
        std::string GetResolutionLabel(int index) const;

    private:
        RuntimeUiController& mRuntimeUiController; // 运行时 UI 控制器引用（管理运行时 UI 屏幕的加载/显示/隐藏）
        UserState& mState;              // 用户状态引用（控制游戏流程状态）
        bool& mAppRunning;              // 应用运行标志引用（置 false 则退出程序）
        AudioSystem* mAudioSystem = nullptr;
        SettingsState mSettingsState;
        bool mHasPresentedFlow = false;
        bool mPresentedSettingsOpen = false;
        GameFlowState mPresentedFlow = GameFlowState::MainMenu;
        std::uint64_t mPresentedRevision = 0;
    };

} // namespace engine
