#include "Runtime/UI/VisualUIEditor/RuntimeUiController.hpp"
#include "Runtime/UserState/UserState.hpp"
#include "Runtime/Core/FrameScheduler.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <string>
#include <vector>

using namespace engine;

namespace {
    void Require(bool condition, const std::string& message) {
        if (!condition) {
            std::fprintf(stderr, "FAIL: %s\n", message.c_str());
            std::exit(1);
        }
    }

    bool FindRect(const UIElement& node, const UIRect& parent, UIElementId id, UIRect& result) {
        const UIRect rect = node.GetType() == UIElementType::Canvas ? parent : node.transform.ComputeRect(parent);
        if (node.GetId() == id) { result = rect; return true; }
        for (const auto& child : node.GetChildren()) {
            if (FindRect(*child, rect, id, result)) return true;
        }
        return false;
    }

    struct Fixture {
        bool running = true;
        UserState state;
        RuntimeDisplaySettings display{1280, 720, false};
        RuntimeDisplaySettings requested;
        bool acceptDisplay = true;
        int applyCalls = 0;
        RuntimeUiController controller{running, state.RuntimeUi()};

        Fixture() {
            controller.Initialize([](const std::string&) -> void* { return nullptr; });
            controller.SetDisplaySettingsCallbacks(
                [this] { return display; },
                [this](const RuntimeDisplaySettings& next) {
                    ++applyCalls;
                    requested = next;
                    if (acceptDisplay) display = next;
                    return acceptDisplay;
                });
            for (const char* name : {"MainMenu", "HUD", "PauseMenu", "Settings", "GameOver", "Win",
                                     "AbilityUnlock", "RespawnPrompt", "UFONews"}) {
                Require(controller.PreloadWidget(Path(name)), std::string("load real asset ") + name);
            }
            Require(controller.SyncGameFlowUi(), "present initial authoritative state");
            Settle();
        }

        static std::string Path(const std::string& name) { return "Assets/ui/" + name + ".ui.json"; }
        UIManager& Manager() { return *controller.GetManager(); }
        UIScreen& Screen(const char* name) {
            UIScreen* screen = Manager().GetScreen(name);
            Require(screen != nullptr, std::string("loaded screen ") + name);
            return *screen;
        }
        template<class T> T& Element(const char* screen, const char* name) {
            auto* element = dynamic_cast<T*>(Screen(screen).FindByName(name));
            Require(element != nullptr, std::string("typed asset element ") + screen + "/" + name);
            return *element;
        }
        void EventInFrame(const std::string& name) {
            Require(Manager().HasEventHandler(name), "registered event " + name);
            Require(controller.DispatchEvent(name), "runtime facade dispatches registered event " + name);
        }
        void NextFrame() {
            state.frame = FrameScheduler::BeginFrame(state.gameFlow, [] {
                Require(false, "reload completion must use the explicit test host");
                return false;
            });
            Require(controller.SyncGameFlowUi(), "frame boundary presents committed state");
        }
        void Event(const std::string& name) {
            EventInFrame(name);
            // Existing scenarios model distinct input frames. In-frame regressions
            // below call EventInFrame and inspect the state before this boundary.
            state.gameFlow.CommitPendingRequests();
            Require(controller.SyncGameFlowUi(), "next input frame presents the submitted event");
        }
        void Settle() {
            // Advance real animation time, without sleeping or creating a GPU/window.
            for (int frame = 0; frame < 180; ++frame) Manager().Update(1.0f / 60.0f);
        }
        void Visible(std::initializer_list<const char*> expected) {
            std::vector<std::string> actual;
            for (const auto& loaded : Manager().GetLoadedScreens()) {
                if (loaded.screen && loaded.screen->IsVisible()) actual.push_back(loaded.screen->GetName());
            }
            std::vector<std::string> wanted;
            for (const auto* name : expected) wanted.emplace_back(name);
            if (actual != wanted) {
                std::string detail = "visible stack expected [";
                for (const auto& name : wanted) detail += name + " ";
                detail += "] actual [";
                for (const auto& name : actual) detail += name + " ";
                Require(false, detail + "]");
            }
            Require(Manager().GetActiveScreen() == (wanted.empty() ? nullptr : Manager().GetScreen(wanted.back())),
                "active screen matches visible stack top");
        }
        void Flow(GameFlowState expected, bool settings = false) {
            Require(state.gameFlow.State() == expected, "expected authoritative game flow state");
            Require(state.gameFlow.IsSettingsOpen() == settings, "expected independent Settings layer");
            Require(state.gameFlow.CanSimulate() == (expected == GameFlowState::Playing && !settings),
                "only Playing without Settings permits simulation");
        }
        void CompleteReload(bool success) {
            Require(state.gameFlow.State() == GameFlowState::Loading, "host sees Loading before reload");
            Require(state.gameFlow.TakeReloadRequest(), "host consumes reload once");
            Require(!state.gameFlow.TakeReloadRequest(), "consumed reload is not available again");
            Require(state.gameFlow.CompleteReload(success), "host reports real completion status");
            Require(controller.SyncGameFlowUi(), "completion presents authoritative state");
            Require(!state.gameFlow.CompleteReload(success), "duplicate completion is rejected");
        }
        void ClickInFrame(const char* screenName, const char* elementName) {
            Settle();
            auto& screen = Screen(screenName);
            Require(screen.IsVisible(), std::string("click screen visible: ") + screenName);
            auto* element = screen.FindByName(elementName);
            Require(element != nullptr, std::string("click target exists: ") + elementName);
            const std::string event = element->events.onClick.empty() ? element->events.onValueChanged : element->events.onClick;
            Require(!event.empty() && Manager().HasEventHandler(event), "asset click event is bound: " + event);
            UIRect rect;
            Require(FindRect(*screen.GetRootCanvas(), {{0, 0}, screen.GetReferenceResolution(), 0}, element->GetId(), rect),
                "button layout is available");
            // Some existing buttons contain decorative child images. Find an actual
            // exposed point using the production hit tester, never bypassing input.
            glm::vec2 point;
            bool found = false;
            for (float y : {0.5f, 0.1f, 0.9f, 0.25f, 0.75f}) {
                for (float x : {0.5f, 0.1f, 0.9f, 0.25f, 0.75f}) {
                    const glm::vec2 candidate = rect.position + rect.size * glm::vec2(x, y);
                    if (Manager().DebugHitTestElement(candidate) == element) {
                        point = candidate;
                        found = true;
                        break;
                    }
                }
                if (found) break;
            }
            Require(found, std::string("asset button has a clickable point: ") + screenName + "/" + elementName);
            Manager().ClearRecentEvents();
            Manager().HandleMouseMove(point);
            Manager().HandleMouseDown();
            Require(Manager().GetPressedElementId() == element->GetId(), "real mouse-down selects target");
            Manager().HandleMouseUp();
            const auto& events = Manager().GetRecentEvents();
            Require(std::any_of(events.begin(), events.end(), [&](const auto& entry) { return entry.eventName == event; }),
                "real mouse-up dispatches JSON event: " + event);
        }
        void Click(const char* screenName, const char* elementName) {
            ClickInFrame(screenName, elementName);
            state.gameFlow.CommitPendingRequests();
            Require(controller.SyncGameFlowUi(), "next input frame presents the real button event");
        }
        void ShowTemporaryScreens() {
            for (const char* name : {"AbilityUnlock", "RespawnPrompt", "UFONews"}) {
                Require(controller.AddWidgetToViewPort(Path(name)), "show temporary game UI");
            }
        }
    };

    void TestMainMenuSettings() {
        Fixture f;
        f.Visible({"MainMenu"});
        f.Event("MenuBack");
        f.Flow(GameFlowState::MainMenu);
        f.Click("MainMenu", "SettingsButton");
        f.Flow(GameFlowState::MainMenu, true);
        f.Visible({"MainMenu", "Settings"});
        f.Click("Settings", "Toggle_001");
        Require(!f.Element<UIToggle>("Settings", "Toggle_001").isOn, "hints edit is pending");
        Require(f.state.preferences.showHints, "pending edit does not change gameplay settings");
        f.Click("Settings", "ResolutionNextButton");
        const std::string pendingResolution = f.Element<UIText>("Settings", "ResolutionValueText").text;
        f.Event("OpenSettings");
        Require(!f.Element<UIToggle>("Settings", "Toggle_001").isOn, "opening settings twice preserves pending edits");
        Require(f.Element<UIText>("Settings", "ResolutionValueText").text == pendingResolution, "duplicate open preserves display choice");
        f.Click("Settings", "Back");
        f.Flow(GameFlowState::MainMenu);
        f.Visible({"MainMenu"});
        Require(f.applyCalls == 0 && f.state.preferences.showHints, "cancel never applies pending settings");
        f.Click("MainMenu", "SettingsButton");
        Require(f.Element<UIToggle>("Settings", "Toggle_001").isOn, "reopen restores applied hints");
        Require(f.Element<UIText>("Settings", "ResolutionValueText").text == "1280 x 720", "reopen restores applied resolution");
        f.Event("MenuBack");
        f.Visible({"MainMenu"});
        f.Flow(GameFlowState::MainMenu);
        std::puts("PASS main-menu settings: real clicks, cancel, duplicate open, MenuBack");
    }

    void TestPauseAndSettingsSources() {
        Fixture f;
        f.Click("MainMenu", "StartButton");
        f.Flow(GameFlowState::Playing);
        f.Visible({"HUD"});
        f.Event("MenuBack");
        f.Flow(GameFlowState::Paused);
        f.Visible({"HUD", "PauseMenu"});
        f.Click("PauseMenu", "SettingsButton");
        f.Flow(GameFlowState::Paused, true);
        f.Visible({"HUD", "PauseMenu", "Settings"});
        f.Click("Settings", "Back");
        f.Flow(GameFlowState::Paused);
        f.Visible({"HUD", "PauseMenu"});
        f.Click("PauseMenu", "ResumeButton");
        f.Flow(GameFlowState::Playing);
        f.Visible({"HUD"});
        f.Event("OpenSettings");
        f.Flow(GameFlowState::Playing, true);
        f.Visible({"HUD", "Settings"});
        f.Event("MenuBack");
        f.Flow(GameFlowState::Playing);
        f.Visible({"HUD"});
        // Repeated quick toggles exercise pending enter/exit animations too.
        for (int repeat = 0; repeat < 5; ++repeat) {
            f.Event("MenuBack");
            f.Event("OpenSettings");
            f.Event("MenuBack");
            f.Event("MenuBack");
            f.Flow(GameFlowState::Playing);
            f.Visible({"HUD"});
        }
        f.Settle();
        f.Visible({"HUD"});
        Require(f.controller.AddWidgetToViewPort(Fixture::Path("AbilityUnlock")), "show a gameplay hint");
        f.Event("MenuBack");
        f.Visible({"HUD", "AbilityUnlock", "PauseMenu"});
        f.Event("OpenSettings");
        f.Visible({"HUD", "AbilityUnlock", "PauseMenu", "Settings"});
        f.Event("MenuBack");
        f.Event("MenuBack");
        f.Visible({"HUD", "AbilityUnlock"});
        f.Flow(GameFlowState::Playing);
        Require(f.controller.AddWidgetToViewPort(Fixture::Path("UFONews")), "show modal gameplay news");
        Require(f.state.gameFlow.CanSimulate(), "a gameplay notice alone does not change simulation state");
        std::puts("PASS pause/settings: all return sources, real resume click, rapid back without overlays");
    }

    void TestApplyAndDisplayFailure() {
        Fixture f;
        f.Click("MainMenu", "SettingsButton");
        f.Click("Settings", "Toggle_001");
        f.Click("Settings", "ResolutionNextButton");
        f.Click("Settings", "DisplayModeButton");
        f.Click("Settings", "Apply");
        Require(f.applyCalls == 1 && f.display.width == 1600 && f.display.height == 900 && f.display.fullscreen,
            "Apply sends pending display values to host callback");
        Require(!f.state.preferences.showHints, "Apply commits pending hints");
        f.Visible({"MainMenu", "Settings"});
        f.Click("Settings", "Back");
        f.Click("MainMenu", "SettingsButton");
        Require(!f.Element<UIToggle>("Settings", "Toggle_001").isOn, "applied hints survive close and reopen");
        Require(f.Element<UIText>("Settings", "ResolutionValueText").text == "1600 x 900", "applied display survives reopen");
        f.acceptDisplay = false;
        f.Click("Settings", "ResolutionNextButton");
        f.Click("Settings", "Apply");
        Require(f.requested.width == 1920 && f.display.width == 1600, "host can reject a resolution change");
        Require(f.Element<UIText>("Settings", "ResolutionValueText").text == "1600 x 900", "rejected display resets pending UI value");
        f.Click("Settings", "Reset");
        Require(f.Element<UIToggle>("Settings", "Toggle_001").isOn && !f.state.preferences.showHints,
            "Reset edits pending values without applying them");
        f.Event("MenuBack");
        f.Flow(GameFlowState::MainMenu);
        f.Visible({"MainMenu"});
        std::puts("PASS settings apply/reset and rejected display callback");
    }

    void TestReloadAndResults() {
        Fixture f;
        f.Click("MainMenu", "StartButton");
        f.ShowTemporaryScreens();
        f.Event("ShowGameOver");
        f.Flow(GameFlowState::GameOver);
        f.Visible({"GameOver"});
        f.Event("MenuBack");
        f.Visible({"GameOver"});
        f.Click("GameOver", "RestartButton");
        f.Flow(GameFlowState::Loading);
        Require(f.state.gameFlow.ReloadTarget() == GameFlowState::Playing, "restart queues the Playing destination");
        f.Visible({});
        f.Event("MenuBack");
        f.Event("ResumeGame");
        f.Flow(GameFlowState::Loading);
        f.Visible({});
        f.CompleteReload(true);
        f.Flow(GameFlowState::Playing);
        f.Visible({"HUD"});
        Require(!f.state.gameFlow.TakeReloadRequest(), "completion does not request another reload");
        f.ShowTemporaryScreens();
        f.Event("ShowVictory");
        f.Flow(GameFlowState::Victory);
        f.Visible({"Win"});
        f.Event("MenuBack");
        f.Visible({"Win"});
        f.Event("BackToMainMenu");
        f.Flow(GameFlowState::Loading);
        Require(f.state.gameFlow.ReloadTarget() == GameFlowState::MainMenu, "return queues the MainMenu destination");
        f.Event("StartGame");
        f.Flow(GameFlowState::Loading);
        f.Visible({});
        f.CompleteReload(true);
        f.Flow(GameFlowState::MainMenu);
        f.Visible({"MainMenu"});
        Require(!f.state.gameFlow.TakeReloadRequest(), "menu completion does not requeue a reload");
        f.Click("MainMenu", "StartButton");
        f.Event("MenuBack");
        f.Click("PauseMenu", "MainMenuButton");
        f.Flow(GameFlowState::Loading);
        Require(f.state.gameFlow.ReloadTarget() == GameFlowState::MainMenu, "real pause-menu button requests menu reload");
        f.CompleteReload(true);
        f.Visible({"MainMenu"});
        f.Settle();
        f.Visible({"MainMenu"});
        std::puts("PASS result screens/reload requests and temporary-screen cleanup");
    }

    void TestFailedReloadRetry() {
        Fixture f;
        f.Click("MainMenu", "StartButton");
        f.Event("ShowGameOver");
        f.Click("GameOver", "RestartButton");
        f.CompleteReload(false);
        f.Flow(GameFlowState::LoadFailed);
        f.Visible({"MainMenu"});
        f.Event("ResumeGame");
        f.Event("MenuBack");
        f.Flow(GameFlowState::LoadFailed);
        f.Click("MainMenu", "StartButton");
        f.Flow(GameFlowState::Loading);
        f.Visible({});
        f.CompleteReload(false);
        f.Flow(GameFlowState::LoadFailed);
        f.Click("MainMenu", "StartButton");
        f.CompleteReload(true);
        f.Flow(GameFlowState::Playing);
        f.Visible({"HUD"});
        f.Event("BackToMainMenu");
        f.CompleteReload(false);
        f.Flow(GameFlowState::LoadFailed);
        f.Visible({"MainMenu"});
        f.Click("MainMenu", "ExitButton");
        Require(!f.running, "failed reload still leaves a working Exit button");
        std::puts("PASS reload failures stop simulation, expose real retry/exit buttons and permit recovery");
    }

    void TestEditorAndQuit() {
        Fixture f;
        Require(!f.Manager().HasEventHandler("OpenEditor") && !f.controller.DispatchEvent("OpenEditor"),
            "runtime game UI has no editor-workspace command");
        f.state.editor.showEngineUi = true;
        f.Event("StartGame");
        f.Flow(GameFlowState::Playing);
        f.Visible({"HUD"});
        f.state.runtimeUi.showRuntimeUi = false;
        f.Event("ShowVictory");
        f.Flow(GameFlowState::Victory);
        f.Visible({"Win"});
        Require(f.state.runtimeUi.showRuntimeUi && f.state.editor.showEngineUi,
            "victory exposes runtime result screen without changing editor workspace visibility");
        f.Event("BackToMainMenu");
        f.CompleteReload(true);
        f.Event("StartGame");
        f.Flow(GameFlowState::Playing);
        f.state.runtimeUi.showRuntimeUi = false;
        f.Event("BackToMainMenu");
        f.CompleteReload(true);
        f.Flow(GameFlowState::MainMenu);
        f.Visible({"MainMenu"});
        Require(f.state.runtimeUi.showRuntimeUi && f.state.editor.showEngineUi,
            "return exposes runtime main menu without changing editor workspace visibility");
        f.Click("MainMenu", "ExitButton");
        Require(!f.running, "actual exit button stops app loop");
        std::puts("PASS runtime flow preserves editor workspace, restores hidden game UI and handles the real exit button");
    }

    void TestGameplayDrivenSynchronization() {
        Fixture f;
        Require(!f.controller.DispatchEvent("Unknown.GameFlow.Event"), "unknown event is rejected by runtime facade");
        Require(f.state.gameFlow.Request(GameFlowCommand::Start), "gameplay requests Start directly");
        Require(f.controller.SyncGameFlowUi(), "presentation can run before the next boundary");
        f.Flow(GameFlowState::MainMenu);
        f.Visible({"MainMenu"});
        f.NextFrame();
        f.Visible({"HUD"});
        f.Event("OpenSettings");
        Require(f.state.gameFlow.Request(GameFlowCommand::Pause), "pause reason changes while Settings stays open");
        f.NextFrame();
        f.Flow(GameFlowState::Paused, true);
        f.Visible({"HUD", "PauseMenu", "Settings"});
        Require(f.state.gameFlow.Request(GameFlowCommand::Resume), "gameplay resumes the pause reason");
        f.NextFrame();
        f.Flow(GameFlowState::Playing, true);
        f.Visible({"HUD", "Settings"});
        f.Event("CloseSettings");
        f.Flow(GameFlowState::Playing);
        f.Visible({"HUD"});
        f.ShowTemporaryScreens();
        Require(f.state.gameFlow.Request(GameFlowCommand::Victory), "scene completion requests Victory directly");
        Require(f.controller.SyncGameFlowUi(), "presentation cannot commit scene-driven Victory");
        f.Flow(GameFlowState::Playing);
        f.Visible({"HUD", "AbilityUnlock", "RespawnPrompt", "UFONews"});
        f.NextFrame();
        f.Flow(GameFlowState::Victory);
        f.Visible({"Win"});
        const auto revision = f.state.gameFlow.Revision();
        Require(f.controller.SyncGameFlowUi(), "repeated frame sync succeeds");
        Require(f.state.gameFlow.Revision() == revision, "presentation never writes authoritative flow");
        f.Settle();
        f.Visible({"Win"});
        std::puts("PASS gameplay and UI use the same authority; frame synchronization is presentation-only");
    }

    void TestDeferredUiEventsAndOrdering() {
        Fixture f;
        const auto menuRevision = f.state.gameFlow.Revision();
        f.ClickInFrame("MainMenu", "StartButton");
        Require(f.controller.SyncGameFlowUi(), "sync during the click frame is presentation-only");
        f.Flow(GameFlowState::MainMenu);
        f.Visible({"MainMenu"});
        Require(f.state.gameFlow.Revision() == menuRevision && f.state.gameFlow.HasPendingRequests(),
            "a real Start click queues work without advancing the displayed revision");
        f.NextFrame();
        f.Flow(GameFlowState::Playing);
        f.Visible({"HUD"});

        const auto playingRevision = f.state.gameFlow.Revision();
        f.EventInFrame("MenuBack");
        f.EventInFrame("OpenSettings");
        f.EventInFrame("OpenSettings");
        Require(f.controller.SyncGameFlowUi(), "sync cannot expose pending pause/settings");
        f.Flow(GameFlowState::Playing);
        f.Visible({"HUD"});
        Require(f.state.gameFlow.Revision() == playingRevision &&
            f.state.gameFlow.PendingState() == GameFlowState::Paused && f.state.gameFlow.PendingSettingsOpen(),
            "rapid menu events route against their pending projection without changing the active frame");
        f.NextFrame();
        f.Flow(GameFlowState::Paused, true);
        f.Visible({"HUD", "PauseMenu", "Settings"});
        Require(f.state.gameFlow.Revision() == playingRevision + 1, "all accepted UI events publish one frame revision");

        f.EventInFrame("MenuBack");
        f.EventInFrame("MenuBack");
        f.Flow(GameFlowState::Paused, true);
        f.Visible({"HUD", "PauseMenu", "Settings"});
        f.NextFrame();
        f.Flow(GameFlowState::Playing);
        f.Visible({"HUD"});
        Require(!f.state.gameFlow.HasPendingRequests(), "two rapid Back events close settings then resume in order");

        f.EventInFrame("ShowVictory");
        f.EventInFrame("MenuBack");
        f.Flow(GameFlowState::Playing);
        f.Visible({"HUD"});
        f.NextFrame();
        f.Flow(GameFlowState::Victory);
        f.Visible({"Win"});
        std::puts("PASS real UI clicks and ordered rapid events publish only at the next frame boundary");
    }

    void TestMissingDestination() {
        Fixture f;
        Require(f.controller.UnloadWidget(Fixture::Path("HUD")), "unload HUD to test actual preload path");
        Require(f.controller.PreloadWidget(Fixture::Path("HUD")), "preload HUD while MainMenu is active");
        f.Visible({"MainMenu"});
        Require(f.controller.UnloadWidget(Fixture::Path("Settings")), "unload settings to exercise real load failure");
        const auto originalDirectory = std::filesystem::current_path();
        // Existing test directory has no Assets tree; real assets stay untouched.
        std::filesystem::current_path(originalDirectory / "Tests" / "UI");
        f.Event("OpenSettings");
        std::filesystem::current_path(originalDirectory);
        f.Flow(GameFlowState::MainMenu);
        f.Visible({"MainMenu"});
        Require(!f.state.gameFlow.HasPendingRequests() && f.state.gameFlow.PendingState() == GameFlowState::MainMenu &&
            !f.state.gameFlow.PendingSettingsOpen(), "failed screen preparation queues no hidden transition");
        std::puts("PASS missing settings asset leaves current state and screen usable");
    }
}

int main() {
    TestMainMenuSettings();
    TestPauseAndSettingsSources();
    TestApplyAndDisplayFailure();
    TestReloadAndResults();
    TestFailedReloadRetry();
    TestEditorAndQuit();
    TestGameplayDrivenSynchronization();
    TestDeferredUiEventsAndOrdering();
    TestMissingDestination();
    std::puts("PASS all runtime UI flow regressions (real assets/router/manager; no window or GPU)");
}
