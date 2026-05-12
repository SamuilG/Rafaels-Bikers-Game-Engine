#include "GameUIEventRouter.hpp"

#include <algorithm>
#include <array>
#include <filesystem>
#include <format>

#include "../EngineUi.hpp"
#include "../../AudioSystem/AudioSystem.hpp"
#include "../../UserState/UserState.hpp"
#include "RuntimeUiController.hpp"
#include "UIElement.hpp"
#include "UIManager.hpp"

namespace engine {

    namespace {

        constexpr const char* kMainMenuUiPath = "Assets/ui/MainMenu.ui.json";
        constexpr const char* kHudUiPath = "Assets/ui/HUD.ui.json";
        constexpr const char* kPauseMenuUiPath = "Assets/ui/PauseMenu.ui.json";
        constexpr const char* kSettingsUiPath = "Assets/ui/Settings.ui.json";
        constexpr const char* kGameOverUiPath = "Assets/ui/GameOver.ui.json";
        constexpr const char* kWinUiPath = "Assets/ui/Win.ui.json";

        constexpr const char* kVolumeSliderName = "VolumeSlider";
        constexpr const char* kShowHintsToggleName = "Toggle_001";
        constexpr const char* kResolutionValueTextName = "ResolutionValueText";
        constexpr const char* kDisplayModeValueTextName = "DisplayModeValueText";
        constexpr const char* kJumpHintElementName = "JumpIcon";
        constexpr const char* kHornHintElementName = "HornIcon";
        constexpr const char* kRadioHintElementName = "RadioIcon";

        constexpr std::array<std::pair<int, int>, 4> kSupportedResolutions{ {
            {1280, 720},
            {1600, 900},
            {1920, 1080},
            {2560, 1440}
        } };

    } // namespace

    GameUIEventRouter::GameUIEventRouter(RuntimeUiController& runtimeUiController, UserState& state, bool& appRunning)
        : mRuntimeUiController(runtimeUiController)
        , mState(state)
        , mAppRunning(appRunning) {
    }

    void GameUIEventRouter::SetAudioSystem(AudioSystem* audioSystem) {
        mAudioSystem = audioSystem;
    }

    void GameUIEventRouter::Bind(UIManager& uiManager) {
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
        uiManager.RegisterEventHandler("Settings.Apply", [this](const std::string& eventName) {
            HandleApplySettings(eventName);
        });
        uiManager.RegisterEventHandler("Settings.Reset", [this](const std::string& eventName) {
            HandleResetSettings(eventName);
        });
        uiManager.RegisterEventHandler("Settings.VolumeChanged", [this](const std::string& eventName) {
            HandleVolumeChanged(eventName);
        });
        uiManager.RegisterEventHandler("Settings.ToggleChanged", [this](const std::string& eventName) {
            HandleToggleChanged(eventName);
        });
        uiManager.RegisterEventHandler("Settings.ResolutionPrev", [this](const std::string& eventName) {
            HandleResolutionPrev(eventName);
        });
        uiManager.RegisterEventHandler("Settings.ResolutionNext", [this](const std::string& eventName) {
            HandleResolutionNext(eventName);
        });
        uiManager.RegisterEventHandler("Settings.DisplayModeToggle", [this](const std::string& eventName) {
            HandleDisplayModeToggle(eventName);
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
        uiManager.RegisterEventHandler("OpenEditor", [this](const std::string& eventName) {
            HandleOpenEditor(eventName);
        });

        uiManager.RegisterEventHandler("TestButton", [this](const std::string& eventName) {
            HandleTestButton(eventName);
        });
    }

    void GameUIEventRouter::RefreshPendingSettingsFromGame() {
        mSettingsState.appliedMasterVolume = mAudioSystem ? mAudioSystem->GetMasterVolume() : 1.0f;
        mSettingsState.pendingMasterVolume = mSettingsState.appliedMasterVolume;
        mSettingsState.appliedShowHints = mState.showHints;
        mSettingsState.pendingShowHints = mSettingsState.appliedShowHints;

        const RuntimeDisplaySettings displaySettings = mRuntimeUiController.QueryDisplaySettings();
        mSettingsState.appliedResolutionIndex = FindResolutionIndex(displaySettings.width, displaySettings.height);
        mSettingsState.pendingResolutionIndex = mSettingsState.appliedResolutionIndex;
        mSettingsState.appliedFullscreen = displaySettings.fullscreen;
        mSettingsState.pendingFullscreen = mSettingsState.appliedFullscreen;
    }

    void GameUIEventRouter::CapturePendingSettingsFromUi() {
        UIManager* uiManager = mRuntimeUiController.GetManager();
        if (!uiManager) {
            return;
        }

        if (UIScreen* settingsScreen = uiManager->GetScreen("Settings")) {
            if (UIElement* volumeElement = settingsScreen->FindByName(kVolumeSliderName)) {
                if (auto* slider = dynamic_cast<UISlider*>(volumeElement)) {
                    mSettingsState.pendingMasterVolume = std::clamp(slider->value, slider->minValue, slider->maxValue);
                }
            }
            if (UIElement* toggleElement = settingsScreen->FindByName(kShowHintsToggleName)) {
                if (auto* toggle = dynamic_cast<UIToggle*>(toggleElement)) {
                    mSettingsState.pendingShowHints = toggle->isOn;
                }
            }
        }
    }

    void GameUIEventRouter::SyncSettingsUi() {
        UIManager* uiManager = mRuntimeUiController.GetManager();
        if (!uiManager) {
            return;
        }

        if (UIScreen* settingsScreen = uiManager->GetScreen("Settings")) {
            if (UIElement* volumeElement = settingsScreen->FindByName(kVolumeSliderName)) {
                if (auto* slider = dynamic_cast<UISlider*>(volumeElement)) {
                    slider->value = std::clamp(mSettingsState.pendingMasterVolume, slider->minValue, slider->maxValue);
                }
            }
            if (UIElement* toggleElement = settingsScreen->FindByName(kShowHintsToggleName)) {
                if (auto* toggle = dynamic_cast<UIToggle*>(toggleElement)) {
                    toggle->isOn = mSettingsState.pendingShowHints;
                }
            }
        }

        const RuntimeUiWidget settingsWidget = mRuntimeUiController.GetWidget(kSettingsUiPath);
        settingsWidget.SetText(
            kResolutionValueTextName,
            RuntimeUiTextOptions{
                .text = GetResolutionLabel(mSettingsState.pendingResolutionIndex)
            });
        settingsWidget.SetText(
            kDisplayModeValueTextName,
            RuntimeUiTextOptions{
                .text = mSettingsState.pendingFullscreen ? std::string("Fullscreen") : std::string("Windowed")
            });
    }

    void GameUIEventRouter::SyncHudHintUi() {
        const bool showJumpHint = mState.showHints && mState.jumpEnabled;
        const bool showHornHint = mState.showHints && mState.hornEnabled;
        const bool showRadioHint = mState.showHints && mState.radioEnabled;

        mRuntimeUiController.SetElementVisible(kHudUiPath, kJumpHintElementName, showJumpHint);
        mRuntimeUiController.SetElementVisible(kHudUiPath, kHornHintElementName, showHornHint);
        mRuntimeUiController.SetElementVisible(kHudUiPath, kRadioHintElementName, showRadioHint);
    }

    void GameUIEventRouter::RestoreSettingsReturnScreen() {
        mRuntimeUiController.RemoveWidgetFromViewPort(kSettingsUiPath);

        if (mSettingsState.returnTarget == SettingsReturnTarget::MainMenu) {
            mState.isGameStarted = false;
            mState.isGamePause = false;
            mState.isGameOver = false;
            mState.isGameWon = false;
            mState.gameFlowState = GameFlowState::MainMenu;
            if (!mRuntimeUiController.IsWidgetVisible(kMainMenuUiPath)) {
                mRuntimeUiController.AddWidgetToViewPort(kMainMenuUiPath);
            }
            return;
        }

        mState.isGameStarted = true;
        mState.isGamePause = true;
        mState.isGameOver = false;
        mState.isGameWon = false;
        mState.gameFlowState = GameFlowState::Paused;
        if (!mRuntimeUiController.IsWidgetVisible(kPauseMenuUiPath)) {
            mRuntimeUiController.AddWidgetToViewPort(kPauseMenuUiPath);
        }
    }

    void GameUIEventRouter::HandleStartGame(const std::string& eventName) {
        mState.isGameStarted = true;
        mState.isGamePause = false;
        mState.isGameOver = false;
        mState.isGameWon = false;
        mState.gameFlowState = GameFlowState::Playing;

        mRuntimeUiController.RemoveWidgetFromViewPort(kMainMenuUiPath);
        mRuntimeUiController.RemoveWidgetFromViewPort(kPauseMenuUiPath);
        mRuntimeUiController.RemoveWidgetFromViewPort(kSettingsUiPath);
        mRuntimeUiController.RemoveWidgetFromViewPort(kWinUiPath);
        mRuntimeUiController.AddWidgetToViewPort(kHudUiPath);

        EngineUi::ShowToast("[ Runtime UI: Start Game ]");
        EngineUi::LogPrint("[RuntimeUI] Routed '{}' -> Playing | MainMenu hidden | HUD visible\n", eventName);
    }

    void GameUIEventRouter::HandleOpenEditor(const std::string& eventName) {
#ifdef GAME_ONLY
        HandleStartGame(eventName);
#else
        mState.isGameStarted = true;
        mState.isGamePause = false;
        mState.isGameOver = false;
        mState.isGameWon = false;
        mState.gameFlowState = GameFlowState::Playing;
        mState.showEngineUi = true;

        mRuntimeUiController.RemoveWidgetFromViewPort(kMainMenuUiPath);
        mRuntimeUiController.RemoveWidgetFromViewPort(kPauseMenuUiPath);
        mRuntimeUiController.RemoveWidgetFromViewPort(kSettingsUiPath);
        mRuntimeUiController.RemoveWidgetFromViewPort(kWinUiPath);
        mRuntimeUiController.AddWidgetToViewPort(kHudUiPath);

        EngineUi::ShowToast("[ Editor Mode ]");
        EngineUi::LogPrint("[RuntimeUI] Routed '{}' -> Playing + Editor UI\n", eventName);
#endif
    }
    // PauseGame：暂停游戏并弹出 PauseMenu 覆盖层。
    void GameUIEventRouter::HandlePauseGame(const std::string& eventName) {
        mState.isGameStarted = true;
        mState.isGamePause = true;
        mState.isGameOver = false;
        mState.isGameWon = false;
        mState.gameFlowState = GameFlowState::Paused;

        mRuntimeUiController.RemoveWidgetFromViewPort(kSettingsUiPath);
        mRuntimeUiController.RemoveWidgetFromViewPort(kWinUiPath);
        mRuntimeUiController.AddWidgetToViewPort(kPauseMenuUiPath);

        EngineUi::ShowToast("[ Runtime UI: Pause ]");
        EngineUi::LogPrint("[RuntimeUI] Routed '{}' -> Paused | PauseMenu visible\n", eventName);
    }

    void GameUIEventRouter::HandleOpenSettings(const std::string& eventName) {
        if (!std::filesystem::exists(kSettingsUiPath)) {
            EngineUi::ShowToast("[ Runtime UI: Settings Missing ]");
            EngineUi::LogPrint("[RuntimeUI] Routed '{}' -> missing settings screen\n", eventName);
            return;
        }

        RefreshPendingSettingsFromGame();
        SyncSettingsUi();

        if (mRuntimeUiController.IsWidgetVisible(kSettingsUiPath)) {
            EngineUi::ShowToast("[ Runtime UI: Settings Already Open ]");
            EngineUi::LogPrint("[RuntimeUI] Routed '{}' -> settings screen already visible\n", eventName);
            return;
        }

        if (!mRuntimeUiController.AddWidgetToViewPort(kSettingsUiPath)) {
            EngineUi::ShowToast("[ Runtime UI: Settings Load Failed ]");
            EngineUi::LogPrint("[RuntimeUI] Routed '{}' -> failed to show settings screen '{}'\n", eventName, kSettingsUiPath);
            return;
        }

        SyncSettingsUi();

        EngineUi::ShowToast("[ Runtime UI: Settings Screen Opened ]");
        EngineUi::LogPrint("[RuntimeUI] Routed '{}' -> Settings | Settings screen pushed and visible\n", eventName);
    }
    // CloseSettings：关闭设置界面，回退到暂停菜单。
    void GameUIEventRouter::HandleCloseSettings(const std::string& eventName) {
        RefreshPendingSettingsFromGame();
        mRuntimeUiController.RemoveWidgetFromViewPort(kSettingsUiPath);

        EngineUi::ShowToast("[ Runtime UI: Close Settings ]");
        EngineUi::LogPrint("[RuntimeUI] Routed '{}' -> settings screen hidden\n", eventName);
    }

    void GameUIEventRouter::HandleApplySettings(const std::string& eventName) {
        CapturePendingSettingsFromUi();

        const auto [width, height] = kSupportedResolutions[std::clamp(
            mSettingsState.pendingResolutionIndex,
            0,
            static_cast<int>(kSupportedResolutions.size()) - 1)];
        const RuntimeDisplaySettings pendingDisplaySettings{
            .width = width,
            .height = height,
            .fullscreen = mSettingsState.pendingFullscreen
        };
        const bool appliedDisplaySettings = mRuntimeUiController.ApplyDisplaySettings(pendingDisplaySettings);

        mSettingsState.appliedMasterVolume = mSettingsState.pendingMasterVolume;
        mSettingsState.appliedShowHints = mSettingsState.pendingShowHints;
        if (appliedDisplaySettings) {
            mSettingsState.appliedResolutionIndex = mSettingsState.pendingResolutionIndex;
            mSettingsState.appliedFullscreen = mSettingsState.pendingFullscreen;
        }
        else {
            mSettingsState.pendingResolutionIndex = mSettingsState.appliedResolutionIndex;
            mSettingsState.pendingFullscreen = mSettingsState.appliedFullscreen;
        }

        mState.showHints = mSettingsState.appliedShowHints;
        SyncHudHintUi();
        if (mAudioSystem) {
            mAudioSystem->SetMasterVolume(mSettingsState.appliedMasterVolume);
        }

        SyncSettingsUi();
        EngineUi::ShowToast(appliedDisplaySettings ? "[ Settings Applied ]" : "[ Display Settings Failed ]");
        EngineUi::LogPrint(
            "[RuntimeUI] Routed '{}' -> apply settings | volume={:.2f} particles={} resolution={} mode={} displayApplied={}\n",
            eventName,
            mSettingsState.appliedMasterVolume,
            mSettingsState.appliedShowHints ? "hints-on" : "hints-off",
            GetResolutionLabel(mSettingsState.pendingResolutionIndex),
            mSettingsState.pendingFullscreen ? "fullscreen" : "windowed",
            appliedDisplaySettings ? "true" : "false");
    }

    void GameUIEventRouter::HandleResetSettings(const std::string& eventName) {
        mSettingsState.pendingMasterVolume = 1.0f;
        mSettingsState.pendingShowHints = true;
        mSettingsState.pendingResolutionIndex = 0;
        mSettingsState.pendingFullscreen = false;
        SyncSettingsUi();

        EngineUi::ShowToast("[ Settings Reset ]");
        EngineUi::LogPrint("[RuntimeUI] Routed '{}' -> reset pending settings\n", eventName);
    }

    void GameUIEventRouter::HandleVolumeChanged(const std::string& eventName) {
        CapturePendingSettingsFromUi();
        EngineUi::LogPrint("[RuntimeUI] Routed '{}' -> pending volume {:.2f}\n", eventName, mSettingsState.pendingMasterVolume);
    }

    void GameUIEventRouter::HandleToggleChanged(const std::string& eventName) {
        CapturePendingSettingsFromUi();
        EngineUi::LogPrint(
            "[RuntimeUI] Routed '{}' -> pending show hints {}\n",
            eventName,
            mSettingsState.pendingShowHints ? "on" : "off");
    }

    void GameUIEventRouter::HandleResolutionPrev(const std::string& eventName) {
        const int maxIndex = static_cast<int>(kSupportedResolutions.size()) - 1;
        mSettingsState.pendingResolutionIndex = std::clamp(mSettingsState.pendingResolutionIndex - 1, 0, maxIndex);
        SyncSettingsUi();
        EngineUi::LogPrint(
            "[RuntimeUI] Routed '{}' -> pending resolution {}\n",
            eventName,
            GetResolutionLabel(mSettingsState.pendingResolutionIndex));
    }

    void GameUIEventRouter::HandleResolutionNext(const std::string& eventName) {
        const int maxIndex = static_cast<int>(kSupportedResolutions.size()) - 1;
        mSettingsState.pendingResolutionIndex = std::clamp(mSettingsState.pendingResolutionIndex + 1, 0, maxIndex);
        SyncSettingsUi();
        EngineUi::LogPrint(
            "[RuntimeUI] Routed '{}' -> pending resolution {}\n",
            eventName,
            GetResolutionLabel(mSettingsState.pendingResolutionIndex));
    }

    void GameUIEventRouter::HandleDisplayModeToggle(const std::string& eventName) {
        mSettingsState.pendingFullscreen = !mSettingsState.pendingFullscreen;
        SyncSettingsUi();
        EngineUi::LogPrint(
            "[RuntimeUI] Routed '{}' -> pending display mode {}\n",
            eventName,
            mSettingsState.pendingFullscreen ? "fullscreen" : "windowed");
    }

    int GameUIEventRouter::FindResolutionIndex(int width, int height) const {
        for (std::size_t index = 0; index < kSupportedResolutions.size(); ++index) {
            const auto [candidateWidth, candidateHeight] = kSupportedResolutions[index];
            if (candidateWidth == width && candidateHeight == height) {
                return static_cast<int>(index);
            }
        }
        return 0;
    }

    std::string GameUIEventRouter::GetResolutionLabel(int index) const {
        const int clampedIndex = std::clamp(index, 0, static_cast<int>(kSupportedResolutions.size()) - 1);
        const auto [width, height] = kSupportedResolutions[clampedIndex];
        return std::format("{} x {}", width, height);
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
        mState.isGameWon = false;
        mState.isGameStarted = false;
        mState.gameFlowState = GameFlowState::MainMenu;

        mRuntimeUiController.RemoveWidgetFromViewPort(kHudUiPath);
        mRuntimeUiController.RemoveWidgetFromViewPort(kPauseMenuUiPath);
        mRuntimeUiController.RemoveWidgetFromViewPort(kSettingsUiPath);
        mRuntimeUiController.RemoveWidgetFromViewPort(kGameOverUiPath);
        mRuntimeUiController.RemoveWidgetFromViewPort(kWinUiPath);
        mRuntimeUiController.AddWidgetToViewPort(kMainMenuUiPath);

        EngineUi::ShowToast("[ Runtime UI: Back To Main Menu ]");
        EngineUi::LogPrint("[RuntimeUI] Routed '{}' -> MainMenu | HUD/Pause hidden\n", eventName);
    }

    void GameUIEventRouter::HandleResumeGame(const std::string& eventName) {
        // Resume：关闭设置层和暂停层，并恢复 HUD 的显示顺序。
        mState.isGameStarted = true;
        mState.isGamePause = false;
        mState.isGameOver = false;
        mState.isGameWon = false;
        mState.gameFlowState = GameFlowState::Playing;

        mRuntimeUiController.RemoveWidgetFromViewPort(kSettingsUiPath);
        mRuntimeUiController.RemoveWidgetFromViewPort(kPauseMenuUiPath);
        mRuntimeUiController.RemoveWidgetFromViewPort(kWinUiPath);
        mRuntimeUiController.AddWidgetToViewPort(kHudUiPath);

        EngineUi::ShowToast("[ Runtime UI: Resume ]");
        EngineUi::LogPrint("[RuntimeUI] Routed '{}' -> Playing | PauseMenu hidden | HUD visible\n", eventName);
    }
    // ShowGameOver：切到 GameOver 状态，隐藏所有游戏中界面并显示结算屏幕。
    void GameUIEventRouter::HandleShowGameOver(const std::string& eventName) {
        mState.isGameStarted = true;
        mState.isGamePause = false;
        mState.isGameOver = true;
        mState.isGameWon = false;
        mState.gameFlowState = GameFlowState::GameOver;

        mRuntimeUiController.RemoveWidgetFromViewPort(kHudUiPath);
        mRuntimeUiController.RemoveWidgetFromViewPort(kPauseMenuUiPath);
        mRuntimeUiController.RemoveWidgetFromViewPort(kSettingsUiPath);
        mRuntimeUiController.RemoveWidgetFromViewPort(kWinUiPath);
        mRuntimeUiController.AddWidgetToViewPort(kGameOverUiPath);

        EngineUi::ShowToast("[ Runtime UI: Game Over ]");
        EngineUi::LogPrint("[RuntimeUI] Routed '{}' -> GameOver | GameOver screen visible\n", eventName);
    }
    // RestartGame：重新开始游戏，重置为 Playing 状态并恢复 HUD。
    void GameUIEventRouter::HandleRestartGame(const std::string& eventName) {
        mState.isGameStarted = true;
        mState.isGamePause = false;
        mState.isGameOver = false;
        mState.isGameWon = false;
        mState.gameFlowState = GameFlowState::Playing;
        mState.restartRequested = true;
        mState.returnToMainMenuRequested = false;

        mRuntimeUiController.RemoveWidgetFromViewPort(kGameOverUiPath);
        mRuntimeUiController.RemoveWidgetFromViewPort(kWinUiPath);
        mRuntimeUiController.RemoveWidgetFromViewPort(kPauseMenuUiPath);
        mRuntimeUiController.RemoveWidgetFromViewPort(kSettingsUiPath);

        EngineUi::ShowToast("[ Runtime UI: Restart Game ]");
        EngineUi::LogPrint("[RuntimeUI] Routed '{}' -> Playing | Restart requested\n", eventName);
    }

    // TestButton：调试用，打印日志和弹出 Toast。
    void GameUIEventRouter::HandleTestButton(const std::string& eventName)
    {
        EngineUi::ShowToast("[ Runtime UI: Test Button Clicked ]");
        EngineUi::LogPrint("[RuntimeUI] Routed '{}' -> test button clicked\n", eventName);
    }

} // namespace engine
