#pragma once

#include <string>

namespace engine {

    class RuntimeUiController;
    class UIManager;
    struct UserState;

    // 运行时 UI 事件到游戏逻辑的路由层。
    // UIManager 只负责在按钮/控件交互后抛出事件名，
    // 真正的状态切换、界面切换和应用退出都在这里完成。
    class GameUIEventRouter {
    public:
        // router 只持有外部系统的引用，不拥有它们的生命周期。
        GameUIEventRouter(RuntimeUiController& runtimeUiController, UserState& state, bool& appRunning);

        // 把当前支持的事件名统一注册到 UIManager。
        void Bind(UIManager& uiManager);

    private:
        // 主菜单里的 StartGame / MainMenu.StartGame 都会落到这里。
        void HandleStartGame(const std::string& eventName);
        // 进入暂停流程并显示 PauseMenu。
        void HandlePauseGame(const std::string& eventName);
        // 打开设置界面，并把 SettingsMenu 压到 PauseMenu 之上。
        void HandleOpenSettings(const std::string& eventName);
        // 关闭设置界面，返回暂停菜单。
        void HandleCloseSettings(const std::string& eventName);
        // 使用引擎现有 appRunning 退出机制关闭程序。
        void HandleQuitGame(const std::string& eventName);
        // 清空当前运行时 UI 栈并重新载入主菜单。
        void HandleBackToMainMenu(const std::string& eventName);
        // 关闭暂停菜单并恢复 HUD 显示。
        void HandleResumeGame(const std::string& eventName);
        // 切换到 GameOver 状态，隐藏所有游戏中界面并显示结算屏幕。
        void HandleShowGameOver(const std::string& eventName);
        // 重新开始游戏：重置为 Playing 状态并显示 HUD。
        void HandleRestartGame(const std::string& eventName);
        // 调试按钮事件，方便验证整条运行时点击链路。
        void HandleTestButton(const std::string& eventName);

    private:
        RuntimeUiController& mRuntimeUiController; // 运行时 UI 控制器引用（管理运行时 UI 屏幕的加载/显示/隐藏）
        UserState& mState;              // 用户状态引用（控制游戏流程状态）
        bool& mAppRunning;              // 应用运行标志引用（置 false 则退出程序）
    };

} // namespace engine
