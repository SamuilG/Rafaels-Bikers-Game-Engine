#include "GameUIEventRouter.hpp"

#include <filesystem>

#include "../EngineUi.hpp"
#include "../../UserState/UserState.hpp"
#include "RuntimeUiController.hpp"
#include "UIManager.hpp"

namespace engine {

    namespace {

        // 路由层默认使用的几张运行时 UI 资源。
        constexpr const char* kMainMenuUiPath = "Assets/ui/MainMenu.ui.json";
        constexpr const char* kHudUiPath = "Assets/ui/HUD.ui.json";
        constexpr const char* kPauseMenuUiPath = "Assets/ui/PauseMenu.ui.json";
        constexpr const char* kSettingsUiPath = "Assets/ui/SettingsMenu.ui.json";
        constexpr const char* kGameOverUiPath = "Assets/ui/GameOver.ui.json";

    } // namespace

    GameUIEventRouter::GameUIEventRouter(RuntimeUiController& runtimeUiController, UserState& state, bool& appRunning)
        : mRuntimeUiController(runtimeUiController)
        , mState(state)
        , mAppRunning(appRunning) {
    }

    void GameUIEventRouter::Bind(UIManager& uiManager) {
        // 同一动作允许绑定多个事件别名，
        // 这样 UI 文件里既可以写短名，也可以写带菜单前缀的名字。
        uiManager.RegisterEventHandler("StartGame", [this](const std::string& eventName) {
            HandleStartGame(eventName);
        });
        uiManager.RegisterEventHandler("MainMenu.StartGame", [this](const std::string& eventName) {
            HandleStartGame(eventName);
        });

        uiManager.RegisterEventHandler("PauseGame", [this](const std::string& eventName) {
            HandlePauseGame(eventName);
        });

        uiManager.RegisterEventHandler("OpenSettings", [this](const std::string& eventName) {
            HandleOpenSettings(eventName);
        });
        uiManager.RegisterEventHandler("OpenOptions", [this](const std::string& eventName) {
            HandleOpenSettings(eventName);
        });
        uiManager.RegisterEventHandler("MainMenu.OpenOptions", [this](const std::string& eventName) {
            HandleOpenSettings(eventName);
        });
        uiManager.RegisterEventHandler("CloseSettings", [this](const std::string& eventName) {
            HandleCloseSettings(eventName);
        });

        uiManager.RegisterEventHandler("QuitGame", [this](const std::string& eventName) {
            HandleQuitGame(eventName);
        });
        uiManager.RegisterEventHandler("ExitGame", [this](const std::string& eventName) {
            HandleQuitGame(eventName);
        });
        uiManager.RegisterEventHandler("MainMenu.ExitGame", [this](const std::string& eventName) {
            HandleQuitGame(eventName);
        });

        uiManager.RegisterEventHandler("BackToMainMenu", [this](const std::string& eventName) {
            HandleBackToMainMenu(eventName);
        });

        uiManager.RegisterEventHandler("ResumeGame", [this](const std::string& eventName) {
            HandleResumeGame(eventName);
        });

        uiManager.RegisterEventHandler("ShowGameOver", [this](const std::string& eventName) {
            HandleShowGameOver(eventName);
        });

        uiManager.RegisterEventHandler("RestartGame", [this](const std::string& eventName) {
            HandleRestartGame(eventName);
        });

        uiManager.RegisterEventHandler("TestButton", [this](const std::string& eventName) {
            HandleTestButton(eventName);
        });
    }

    void GameUIEventRouter::HandleStartGame(const std::string& eventName) {
        // StartGame：切到游戏态，隐藏主菜单，并把 HUD 提到最上层。
        mState.isGameStarted = true;
        mState.isGamePause = false;
        mState.isGameOver = false;
        mState.gameFlowState = GameFlowState::Playing;

        mRuntimeUiController.RemoveWidgetFromViewPort(kMainMenuUiPath);
        mRuntimeUiController.RemoveWidgetFromViewPort(kPauseMenuUiPath);
        mRuntimeUiController.RemoveWidgetFromViewPort(kSettingsUiPath);
        mRuntimeUiController.AddWidgetToViewPort(kHudUiPath);

        EngineUi::ShowToast("[ Runtime UI: Start Game ]");
        EngineUi::LogPrint("[RuntimeUI] Routed '{}' -> Playing | MainMenu hidden | HUD visible\n", eventName);
    }

    // PauseGame：暂停游戏并弹出 PauseMenu 覆盖层。
    void GameUIEventRouter::HandlePauseGame(const std::string& eventName) {
        mState.isGameStarted = true;
        mState.isGamePause = true;
        mState.isGameOver = false;
        mState.gameFlowState = GameFlowState::Paused;

        mRuntimeUiController.RemoveWidgetFromViewPort(kSettingsUiPath);
        mRuntimeUiController.AddWidgetToViewPort(kPauseMenuUiPath);

        EngineUi::ShowToast("[ Runtime UI: Pause ]");
        EngineUi::LogPrint("[RuntimeUI] Routed '{}' -> Paused | PauseMenu visible\n", eventName);
    }

    void GameUIEventRouter::HandleOpenSettings(const std::string& eventName) {
        // 选项菜单作为运行时堆栈上的模态层显示在 PauseMenu 之上。
        if (!std::filesystem::exists(kSettingsUiPath)) {
            EngineUi::ShowToast("[ Runtime UI: Settings Missing ]");
            EngineUi::LogPrint("[RuntimeUI] Routed '{}' -> missing settings screen\n", eventName);
            return;
        }

        mState.isGameStarted = true;
        mState.isGamePause = true;
        mState.isGameOver = false;
        mState.gameFlowState = GameFlowState::Settings;

        mRuntimeUiController.AddWidgetToViewPort(kPauseMenuUiPath);
        mRuntimeUiController.AddWidgetToViewPort(kSettingsUiPath);

        EngineUi::ShowToast("[ Runtime UI: Settings ]");
        EngineUi::LogPrint("[RuntimeUI] Routed '{}' -> Settings | SettingsMenu visible\n", eventName);
    }

    // CloseSettings：关闭设置界面，回退到暂停菜单。
    void GameUIEventRouter::HandleCloseSettings(const std::string& eventName) {
        mState.isGameStarted = true;
        mState.isGamePause = true;
        mState.isGameOver = false;
        mState.gameFlowState = GameFlowState::Paused;

        mRuntimeUiController.RemoveWidgetFromViewPort(kSettingsUiPath);
        mRuntimeUiController.AddWidgetToViewPort(kPauseMenuUiPath);

        EngineUi::ShowToast("[ Runtime UI: Close Settings ]");
        EngineUi::LogPrint("[RuntimeUI] Routed '{}' -> Paused | SettingsMenu hidden\n", eventName);
    }

    // QuitGame / ExitGame：直接关闭应用。
    void GameUIEventRouter::HandleQuitGame(const std::string& eventName) {
        mAppRunning = false;
        EngineUi::LogPrint("[RuntimeUI] Routed '{}' -> exit application\n", eventName);
    }

    void GameUIEventRouter::HandleBackToMainMenu(const std::string& eventName) {
        // 回主菜单时保留已加载屏幕，只重置可见层级，方便后续再次切回 HUD / Pause。
        mState.isGamePause = false;
        mState.isGameOver = false;
        mState.isGameStarted = false;
        mState.gameFlowState = GameFlowState::MainMenu;

        mRuntimeUiController.RemoveWidgetFromViewPort(kHudUiPath);
        mRuntimeUiController.RemoveWidgetFromViewPort(kPauseMenuUiPath);
        mRuntimeUiController.RemoveWidgetFromViewPort(kSettingsUiPath);
        mRuntimeUiController.RemoveWidgetFromViewPort(kGameOverUiPath);
        mRuntimeUiController.AddWidgetToViewPort(kMainMenuUiPath);

        EngineUi::ShowToast("[ Runtime UI: Back To Main Menu ]");
        EngineUi::LogPrint("[RuntimeUI] Routed '{}' -> MainMenu | HUD/Pause hidden\n", eventName);
    }

    void GameUIEventRouter::HandleResumeGame(const std::string& eventName) {
        // Resume：关闭设置层和暂停层，并恢复 HUD 的显示顺序。
        mState.isGameStarted = true;
        mState.isGamePause = false;
        mState.isGameOver = false;
        mState.gameFlowState = GameFlowState::Playing;

        mRuntimeUiController.RemoveWidgetFromViewPort(kSettingsUiPath);
        mRuntimeUiController.RemoveWidgetFromViewPort(kPauseMenuUiPath);
        mRuntimeUiController.AddWidgetToViewPort(kHudUiPath);

        EngineUi::ShowToast("[ Runtime UI: Resume ]");
        EngineUi::LogPrint("[RuntimeUI] Routed '{}' -> Playing | PauseMenu hidden | HUD visible\n", eventName);
    }

    // ShowGameOver：切到 GameOver 状态，隐藏所有游戏中界面并显示结算屏幕。
    void GameUIEventRouter::HandleShowGameOver(const std::string& eventName) {
        mState.isGameStarted = true;
        mState.isGamePause = false;
        mState.isGameOver = true;
        mState.gameFlowState = GameFlowState::GameOver;

        mRuntimeUiController.RemoveWidgetFromViewPort(kHudUiPath);
        mRuntimeUiController.RemoveWidgetFromViewPort(kPauseMenuUiPath);
        mRuntimeUiController.RemoveWidgetFromViewPort(kSettingsUiPath);
        mRuntimeUiController.AddWidgetToViewPort(kGameOverUiPath);

        EngineUi::ShowToast("[ Runtime UI: Game Over ]");
        EngineUi::LogPrint("[RuntimeUI] Routed '{}' -> GameOver | GameOver screen visible\n", eventName);
    }

    // RestartGame：重新开始游戏，重置为 Playing 状态并恢复 HUD。
    void GameUIEventRouter::HandleRestartGame(const std::string& eventName) {
        mState.isGameStarted = true;
        mState.isGamePause = false;
        mState.isGameOver = false;
        mState.gameFlowState = GameFlowState::Playing;

        mRuntimeUiController.RemoveWidgetFromViewPort(kGameOverUiPath);
        mRuntimeUiController.RemoveWidgetFromViewPort(kPauseMenuUiPath);
        mRuntimeUiController.RemoveWidgetFromViewPort(kSettingsUiPath);
        mRuntimeUiController.AddWidgetToViewPort(kHudUiPath);

        EngineUi::ShowToast("[ Runtime UI: Restart Game ]");
        EngineUi::LogPrint("[RuntimeUI] Routed '{}' -> Playing | Restarted\n", eventName);
    }

    // TestButton：调试用，打印日志和弹出 Toast。
    void GameUIEventRouter::HandleTestButton(const std::string& eventName)
    {
        EngineUi::ShowToast("[ Runtime UI: Test Button Clicked ]");
        EngineUi::LogPrint("[RuntimeUI] Routed '{}' -> test button clicked\n", eventName);
    }

} // namespace engine
