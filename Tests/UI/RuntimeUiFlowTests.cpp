#include "Runtime/UI/VisualUIEditor/RuntimeUiController.hpp"
#include "Runtime/UserState/UserState.hpp"

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
        RuntimeUiController controller{running, state};

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
            Event("ResetToMainMenu");
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
        void Event(const std::string& name) {
            Require(Manager().HasEventHandler(name), "registered event " + name);
            Manager().TriggerEvent(name);
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
        void Flow(GameFlowState expected) {
            Require(state.gameFlowState == expected, "expected game flow state");
            Require(state.IsGameplayActive() == (expected == GameFlowState::Playing &&
                !state.restartRequested && !state.returnToMainMenuRequested), "gameplay pause/reload gate");
            if (expected != GameFlowState::Playing) {
                Require(Manager().BlocksGameplayInput(), "menus and result screens block gameplay input");
            }
            if (expected != GameFlowState::Settings) {
                Require(state.isGameStarted == (expected != GameFlowState::MainMenu), "legacy started flag matches flow");
                Require(state.isGamePause == (expected == GameFlowState::Paused), "legacy pause flag matches flow");
                Require(state.isGameOver == (expected == GameFlowState::GameOver), "legacy game-over flag matches flow");
                Require(state.isGameWon == (expected == GameFlowState::Victory), "legacy victory flag matches flow");
            }
        }
        void Click(const char* screenName, const char* elementName) {
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
        f.Flow(GameFlowState::Settings);
        f.Visible({"MainMenu", "Settings"});
        f.Click("Settings", "Toggle_001");
        Require(!f.Element<UIToggle>("Settings", "Toggle_001").isOn, "hints edit is pending");
        Require(f.state.showHints, "pending edit does not change gameplay settings");
        f.Click("Settings", "ResolutionNextButton");
        const std::string pendingResolution = f.Element<UIText>("Settings", "ResolutionValueText").text;
        f.Event("OpenSettings");
        Require(!f.Element<UIToggle>("Settings", "Toggle_001").isOn, "opening settings twice preserves pending edits");
        Require(f.Element<UIText>("Settings", "ResolutionValueText").text == pendingResolution, "duplicate open preserves display choice");
        f.Click("Settings", "Back");
        f.Flow(GameFlowState::MainMenu);
        f.Visible({"MainMenu"});
        Require(f.applyCalls == 0 && f.state.showHints, "cancel never applies pending settings");
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
        Require(!f.Manager().BlocksGameplayInput(), "HUD allows gameplay input");
        f.Event("MenuBack");
        f.Flow(GameFlowState::Paused);
        f.Visible({"HUD", "PauseMenu"});
        f.Click("PauseMenu", "SettingsButton");
        f.Flow(GameFlowState::Settings);
        f.Visible({"HUD", "PauseMenu", "Settings"});
        f.Click("Settings", "Back");
        f.Flow(GameFlowState::Paused);
        f.Visible({"HUD", "PauseMenu"});
        f.Click("PauseMenu", "ResumeButton");
        f.Flow(GameFlowState::Playing);
        f.Visible({"HUD"});
        f.Event("OpenSettings");
        f.Flow(GameFlowState::Settings);
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
        Require(!f.Manager().BlocksGameplayInput(), "ability hints allow gameplay input");
        Require(f.controller.AddWidgetToViewPort(Fixture::Path("UFONews")), "show modal gameplay news");
        Require(f.Manager().BlocksGameplayInput(), "UFONews blocks gameplay input");
        Require(f.state.IsGameplayActive(), "UFONews input capture leaves simulation active");
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
        Require(!f.state.showHints, "Apply commits pending hints");
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
        Require(f.Element<UIToggle>("Settings", "Toggle_001").isOn && !f.state.showHints,
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
        Require(f.state.restartRequested && !f.state.returnToMainMenuRequested, "restart queues exactly the restart request");
        Require(!f.state.IsGameplayActive(), "game stays stopped while restart is pending");
        f.Event("MenuBack");
        f.Event("ResumeGame");
        f.Visible({"GameOver"});
        // The host consumes its request before signaling completion.
        f.state.restartRequested = false;
        f.Event("ResetToPlaying");
        f.Flow(GameFlowState::Playing);
        f.Visible({"HUD"});
        Require(!f.state.restartRequested && !f.state.returnToMainMenuRequested, "reset-complete must not request another reload");
        f.ShowTemporaryScreens();
        f.Event("ShowVictory");
        f.Flow(GameFlowState::Victory);
        f.Visible({"Win"});
        f.Event("MenuBack");
        f.Visible({"Win"});
        f.Event("BackToMainMenu");
        Require(f.state.returnToMainMenuRequested && !f.state.restartRequested, "return queues the menu reload request");
        Require(!f.state.IsGameplayActive(), "game stops while returning to main menu");
        f.Event("StartGame");
        f.Visible({"MainMenu"});
        f.Flow(GameFlowState::MainMenu);
        f.state.returnToMainMenuRequested = false;
        f.Event("ResetToMainMenu");
        f.Flow(GameFlowState::MainMenu);
        f.Visible({"MainMenu"});
        Require(!f.state.restartRequested && !f.state.returnToMainMenuRequested, "menu reset does not requeue a reload");
        f.Click("MainMenu", "StartButton");
        f.Event("MenuBack");
        f.Click("PauseMenu", "MainMenuButton");
        Require(f.state.returnToMainMenuRequested, "real pause-menu button reaches return request");
        f.Visible({"MainMenu"});
        std::puts("PASS result screens/reload requests and temporary-screen cleanup");
    }

    void TestEditorAndQuit() {
        Fixture f;
        f.Event("OpenEditor");
        f.Flow(GameFlowState::Playing);
        f.Visible({"HUD"});
#ifdef GAME_ONLY
        Require(!f.state.showEngineUi, "Game-only build does not enable editor");
#else
        Require(f.state.showEngineUi, "editor entry enables editor UI");
#endif
        f.state.showRuntimeUi = false;
        f.Event("ShowVictory");
        f.Flow(GameFlowState::Victory);
        f.Visible({"Win"});
        Require(f.state.showRuntimeUi && !f.state.showEngineUi,
            "editor victory exposes runtime result screen and hides editor");
        f.Event("ResetToMainMenu");
        f.Event("OpenEditor");
        f.Flow(GameFlowState::Playing);
        f.state.showRuntimeUi = false;
        f.Event("ResetToMainMenu");
        f.Flow(GameFlowState::MainMenu);
        f.Visible({"MainMenu"});
        Require(f.state.showRuntimeUi && !f.state.showEngineUi,
            "reset from editor exposes runtime main menu and hides editor");
        f.Click("MainMenu", "ExitButton");
        Require(!f.running, "actual exit button stops app loop");
        std::puts("PASS editor configuration and real exit button");
    }

    void TestMissingDestination() {
        Fixture f;
        Require(f.controller.UnloadWidget(Fixture::Path("Settings")), "unload settings to exercise real load failure");
        const auto originalDirectory = std::filesystem::current_path();
        // Existing test directory has no Assets tree; real assets stay untouched.
        std::filesystem::current_path(originalDirectory / "Tests" / "UI");
        f.Event("OpenSettings");
        std::filesystem::current_path(originalDirectory);
        f.Flow(GameFlowState::MainMenu);
        f.Visible({"MainMenu"});
        std::puts("PASS missing settings asset leaves current state and screen usable");
    }
}

int main() {
    TestMainMenuSettings();
    TestPauseAndSettingsSources();
    TestApplyAndDisplayFailure();
    TestReloadAndResults();
    TestEditorAndQuit();
    TestMissingDestination();
    std::puts("PASS all runtime UI flow regressions (real assets/router/manager; no window or GPU)");
}
