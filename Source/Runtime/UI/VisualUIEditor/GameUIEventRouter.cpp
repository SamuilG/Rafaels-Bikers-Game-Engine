#include "GameUIEventRouter.hpp"

#include <algorithm>
#include <array>
#include <format>
#include <vector>

#include "../EngineUi.hpp"
#include "../../AudioSystem/AudioSystem.hpp"
#include "../../UserState/StateViews.hpp"
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

        std::vector<const char*> FlowScreens(GameFlowState flow, bool settingsOpen) {
            std::vector<const char*> paths;
            switch (flow) {
            case GameFlowState::MainMenu:
            case GameFlowState::LoadFailed: paths.push_back(kMainMenuUiPath); break;
            case GameFlowState::Playing: paths.push_back(kHudUiPath); break;
            case GameFlowState::Paused:
                paths.push_back(kHudUiPath);
                paths.push_back(kPauseMenuUiPath);
                break;
            case GameFlowState::GameOver: paths.push_back(kGameOverUiPath); break;
            case GameFlowState::Victory: paths.push_back(kWinUiPath); break;
            case GameFlowState::Loading: break;
            }
            if (settingsOpen) paths.push_back(kSettingsUiPath);
            return paths;
        }

        bool IsGameplayScreenFlow(GameFlowState flow) {
            return flow == GameFlowState::Playing || flow == GameFlowState::Paused;
        }

    } // namespace

    GameUIEventRouter::GameUIEventRouter(RuntimeUiController& runtimeUiController, RuntimeUiStateView state, bool& appRunning)
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

        uiManager.RegisterEventHandler("ShowVictory", [this](const std::string& eventName) {
            HandleShowVictory(eventName);
        });
        uiManager.RegisterEventHandler("MenuBack", [this](const std::string& eventName) {
            HandleMenuBack(eventName);
        });

        uiManager.RegisterEventHandler("RestartGame", [this](const std::string& eventName) {
            HandleRestartGame(eventName);
        });
        uiManager.RegisterEventHandler("TestButton", [this](const std::string& eventName) {
            HandleTestButton(eventName);
        });
    }

    void GameUIEventRouter::RefreshPendingSettingsFromGame() {
        mSettingsState.appliedMasterVolume = mAudioSystem ? mAudioSystem->GetMasterVolume() : 1.0f;
        mSettingsState.pendingMasterVolume = mSettingsState.appliedMasterVolume;
        mSettingsState.appliedShowHints = mState.preferences.showHints;
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
        if (!mRuntimeUiController.IsWidgetLoaded(kHudUiPath)) return;
        const bool showJumpHint = mState.preferences.showHints && mState.player.State().jumpEnabled;
        const bool showHornHint = mState.preferences.showHints && mState.player.State().hornEnabled;
        const bool showRadioHint = mState.preferences.showHints && mState.player.State().radioEnabled;

        mRuntimeUiController.SetElementVisible(kHudUiPath, kJumpHintElementName, showJumpHint);
        mRuntimeUiController.SetElementVisible(kHudUiPath, kHornHintElementName, showHornHint);
        mRuntimeUiController.SetElementVisible(kHudUiPath, kRadioHintElementName, showRadioHint);
    }

    bool GameUIEventRouter::PrepareFlowScreens(GameFlowState flow, bool settingsOpen) {
        for (const char* path : FlowScreens(flow, settingsOpen)) {
            if (!mRuntimeUiController.PreloadWidget(path)) {
                EngineUi::ShowToast("[ Runtime UI: Screen Load Failed ]");
                EngineUi::LogPrint("[RuntimeUI] Cannot prepare flow screen '{}'\n", path);
                return false;
            }
        }
        return true;
    }

    bool GameUIEventRouter::RequestFlow(GameFlowCommand command, const std::string& eventName) {
        // Validate the value model before loading assets; only the authoritative
        // instance queues a real scene reload.
        auto proposed = mState.gameFlow;
        if (!proposed.Request(command)) return false;
        const GameFlowState preparedFlow = proposed.State() == GameFlowState::Loading
            ? proposed.ReloadTarget() : proposed.State();
        if (!PrepareFlowScreens(preparedFlow, proposed.IsSettingsOpen())) return false;
        if (!mState.gameFlow.Request(command)) return false;
        EngineUi::LogPrint("[RuntimeUI] Routed '{}' through GameFlowController\n", eventName);
        return SyncGameFlowUi();
    }

    bool GameUIEventRouter::SyncGameFlowUi() {
        UIManager* manager = mRuntimeUiController.GetManager();
        if (!manager) return false;
        const auto& flow = mState.gameFlow;
        if (mHasPresentedFlow && mPresentedRevision == flow.Revision()) return true;

        const GameFlowState state = flow.State();
        const bool settingsOpen = flow.IsSettingsOpen();
        if (!PrepareFlowScreens(state, settingsOpen)) return false;
        const auto desiredPaths = FlowScreens(state, settingsOpen);
        const bool keepGameplayScreens = mHasPresentedFlow &&
            IsGameplayScreenFlow(mPresentedFlow) && IsGameplayScreenFlow(state);
        const bool clearSessionScreens = !mHasPresentedFlow ||
            (mPresentedFlow != state && !keepGameplayScreens);

        // Cancel stale modals immediately on results/reload. Pause/settings keep
        // the level's temporary popups and the original HUD render order.
        constexpr std::array<const char*, 6> coreScreens{
            "MainMenu", "HUD", "PauseMenu", "Settings", "GameOver", "Win"
        };
        for (const auto& loaded : manager->GetLoadedScreens()) {
            if (!loaded.screen) continue;
            const std::string& name = loaded.screen->GetName();
            const bool coreScreen = std::find(coreScreens.begin(), coreScreens.end(), name) != coreScreens.end();
            const bool desired = std::any_of(desiredPaths.begin(), desiredPaths.end(), [&](const char* path) {
                return RuntimeUiController::BuildScreenNameFromPath(path) == name;
            });
            if (clearSessionScreens || (coreScreen && !desired)) {
                manager->HideScreenImmediately(name);
            }
        }
        if (settingsOpen && !mPresentedSettingsOpen) {
            RefreshPendingSettingsFromGame();
            SyncSettingsUi();
        }
        else if (!settingsOpen && mPresentedSettingsOpen) {
            RefreshPendingSettingsFromGame();
        }
        for (const char* path : desiredPaths) {
            if (!mRuntimeUiController.IsWidgetVisible(path)) {
                mRuntimeUiController.AddWidgetToViewPort(path);
            }
        }
        // An independent pause reason may add PauseMenu beneath an open
        // Settings overlay. Restore its order without restarting any animation.
        if (settingsOpen && manager->GetActiveScreen() != manager->GetScreen("Settings")) {
            manager->PushScreen("Settings", false);
        }
        SyncHudHintUi();
        mState.runtimeUi.showRuntimeUi = true;
        mPresentedFlow = state;
        mPresentedSettingsOpen = settingsOpen;
        mPresentedRevision = flow.Revision();
        mHasPresentedFlow = true;
        return true;
    }

    void GameUIEventRouter::HandleStartGame(const std::string& eventName) {
        if (RequestFlow(GameFlowCommand::Start, eventName)) {
            EngineUi::ShowToast("[ Runtime UI: Start Game ]");
        }
    }

    void GameUIEventRouter::HandlePauseGame(const std::string& eventName) {
        RequestFlow(GameFlowCommand::Pause, eventName);
    }

    void GameUIEventRouter::HandleOpenSettings(const std::string& eventName) {
        // Duplicate open preserves pending values and the existing source.
        if (mState.gameFlow.IsSettingsOpen()) return;
        RequestFlow(GameFlowCommand::OpenSettings, eventName);
    }

    void GameUIEventRouter::HandleCloseSettings(const std::string& eventName) {
        RequestFlow(GameFlowCommand::CloseSettings, eventName);
    }

    void GameUIEventRouter::HandleMenuBack(const std::string& eventName) {
        if (mState.gameFlow.IsSettingsOpen()) {
            HandleCloseSettings(eventName);
        }
        else if (mState.gameFlow.State() == GameFlowState::Playing) {
            HandlePauseGame(eventName);
        }
        else if (mState.gameFlow.State() == GameFlowState::Paused) {
            HandleResumeGame(eventName);
        }
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

        mState.preferences.showHints = mSettingsState.appliedShowHints;
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
        RequestFlow(GameFlowCommand::ReturnToMainMenu, eventName);
    }

    void GameUIEventRouter::HandleResumeGame(const std::string& eventName) {
        RequestFlow(GameFlowCommand::Resume, eventName);
    }

    void GameUIEventRouter::HandleShowGameOver(const std::string& eventName) {
        RequestFlow(GameFlowCommand::GameOver, eventName);
    }

    void GameUIEventRouter::HandleShowVictory(const std::string& eventName) {
        RequestFlow(GameFlowCommand::Victory, eventName);
    }

    void GameUIEventRouter::HandleRestartGame(const std::string& eventName) {
        RequestFlow(GameFlowCommand::Restart, eventName);
    }

    // TestButton：调试用，打印日志和弹出 Toast。
    void GameUIEventRouter::HandleTestButton(const std::string& eventName)
    {
        EngineUi::ShowToast("[ Runtime UI: Test Button Clicked ]");
        EngineUi::LogPrint("[RuntimeUI] Routed '{}' -> test button clicked\n", eventName);
    }

} // namespace engine
