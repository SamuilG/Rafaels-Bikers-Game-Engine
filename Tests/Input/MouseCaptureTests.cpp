#include "Runtime/Input/InputSystem.hpp"
#include <iostream>
#include <stdexcept>
#include <unordered_map>

namespace {
int middle = GLFW_RELEASE;
int mode = GLFW_CURSOR_NORMAL;
int changes = 0;
int left = GLFW_RELEASE;
bool focused = true;
std::unordered_map<int, int> keys;
double cursor = 0;
void Require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
}
extern "C" {
int glfwInit() { return GLFW_TRUE; }
int glfwUpdateGamepadMappings(const char*) { return GLFW_TRUE; }
void glfwPollEvents() {}
int glfwGetKey(GLFWwindow*, int key) { return keys[key]; }
int glfwGetWindowAttrib(GLFWwindow*, int) { return focused ? GLFW_TRUE : GLFW_FALSE; }
int glfwJoystickPresent(int) { return GLFW_FALSE; }
int glfwJoystickIsGamepad(int) { return GLFW_FALSE; }
int glfwGetGamepadState(int, GLFWgamepadstate*) { return GLFW_FALSE; }
GLFWscrollfun glfwSetScrollCallback(GLFWwindow*, GLFWscrollfun) { return nullptr; }
void glfwGetCursorPos(GLFWwindow*, double* x, double* y) { *x = cursor; *y = cursor; }
int glfwGetMouseButton(GLFWwindow*, int button) {
    return button == GLFW_MOUSE_BUTTON_MIDDLE ? middle : button == GLFW_MOUSE_BUTTON_LEFT ? left : GLFW_RELEASE;
}
void glfwSetInputMode(GLFWwindow*, int, int value) { mode = value; ++changes; cursor += 100; }
int glfwGetInputMode(GLFWwindow*, int) { return mode; }
}
int main() {
    try {
        engine::InputSystem input;
        input.Init();
        input.SetWindow(reinterpret_cast<GLFWwindow*>(1));
        input.Update(0.016f);
        Require(!input.IsMouseCaptured() && changes == 0, "Idle must not capture");
        middle = GLFW_PRESS;
        input.Update(0.016f);
        Require(input.IsMouseCaptured() && changes == 1, "First press must capture exactly once");
        Require(input.GetMouseDelta() == glm::vec2(0), "Capture frame must discard cursor warp");
        input.Update(0.016f);
        Require(input.GetMouseDelta() == glm::vec2(0), "Next frame must discard cursor warp");
        input.SetGameplayInputEnabled(false);
        input.ResetForNewSession();
        for (int i = 0; i < 5; ++i) input.Update(0.016f);
        Require(input.IsMouseCaptured() && changes == 1, "Holding, gameplay gating and session reset must preserve capture");
        middle = GLFW_RELEASE;
        input.Update(0.016f);
        Require(input.IsMouseCaptured() && changes == 1, "Button release must preserve capture");
        middle = GLFW_PRESS;
        input.Update(0.016f);
        Require(!input.IsMouseCaptured() && changes == 2, "Second press must release even when gameplay input is disabled");
        input.Update(0.016f);
        Require(!input.IsMouseCaptured() && changes == 2, "Holding second press must not recapture");
        std::cout << "PASS middle-button edges, capture persistence and cursor warp suppression\n";

        using Owner = engine::InputSystem::MouseCaptureOwner;
        const engine::InputSystem::MouseCaptureRegion game{100, 100, 200, 200};
        middle = GLFW_RELEASE; input.Update(0.016f);
        cursor = 150;
        input.ConfigureMouseCapture(false, game, Owner::GameViewport);
        middle = GLFW_PRESS; input.Update(0.016f);
        Require(!input.IsMouseCaptured(), "Menus and paused/stopped sessions cannot capture");
        middle = GLFW_RELEASE; input.Update(0.016f);
        input.ConfigureMouseCapture(true, game, Owner::GameViewport);
        cursor = 50;
        middle = GLFW_PRESS; input.Update(0.016f);
        Require(!input.IsMouseCaptured(), "Middle clicks outside the active viewport cannot capture");
        middle = GLFW_RELEASE; input.Update(0.016f);
        cursor = 150; input.SetMouseCaptured(true);
        Require(input.IsMouseCaptured(), "A Game canvas click can request real disabled-cursor capture");
        input.Update(0.016f);
        cursor = 10000; input.Update(0.016f);
        Require(input.IsMouseCaptured() && input.GetMouseDelta().x > 0,
            "Unbounded relative coordinates remain game input instead of releasing capture");
        input.ConfigureMouseCapture(true, game, Owner::GameViewport);
        Require(input.IsMouseCaptured(), "The same viewport retains capture despite virtual coordinates leaving its bounds");
        std::cout << "PASS host/viewport capture scope and unlimited relative mouse input\n";

        keys[GLFW_KEY_LEFT_SHIFT] = keys[GLFW_KEY_F1] = GLFW_PRESS;
        input.Update(0.016f);
        Require(!input.IsMouseCaptured() && input.GetMouseDelta() == glm::vec2(0),
            "Shift+F1 releases capture and suppresses the cursor warp");
        input.Update(0.016f);
        Require(!input.IsMouseCaptured(), "Holding the release shortcut does not recapture");
        keys.clear(); input.Update(0.016f);
        cursor = 150; input.SetMouseCaptured(true);
        input.ConfigureMouseCapture(true, game, Owner::SceneViewport);
        Require(!input.IsMouseCaptured(), "Switching viewport owners releases the old owner's mouse");
        cursor = 150; input.SetMouseCaptured(true);
        input.ConfigureMouseCapture(false);
        Require(!input.IsMouseCaptured(), "Pause/Stop or a focus-taking editor panel releases capture");
        input.ConfigureMouseCapture(true, game, Owner::GameViewport);
        cursor = 150; input.SetMouseCaptured(true);
        keys[GLFW_KEY_ESCAPE] = GLFW_PRESS; input.Update(0.016f);
        Require(!input.IsMouseCaptured() && input.IsActionPressed("MenuBack"),
            "Escape releases the cursor without swallowing the game pause command");
        keys.clear(); input.Update(0.016f);
        std::cout << "PASS explicit release, viewport owner change, Pause/Stop and Escape\n";

        cursor = 150; input.SetMouseCaptured(true);
        input.SetGameplayInputEnabled(true);
        keys[GLFW_KEY_W] = left = GLFW_PRESS;
        input.Update(0.016f);
        Require(input.IsGameplayActionHeld("MoveForward") && input.IsGameplayActionHeld("pedal0"),
            "Focused gameplay receives held keys and pedal buttons");
        focused = false; input.Update(0.016f);
        Require(!input.IsMouseCaptured() && !input.IsGameplayInputEnabled() &&
            !input.IsActionHeld("MoveForward") && !input.IsActionHeld("pedal0") &&
            input.GetMouseDelta() == glm::vec2(0),
            "Alt+Tab clears capture, held controls, gameplay forwarding and look delta");
        focused = true; middle = GLFW_PRESS; input.Update(0.016f);
        Require(!input.IsMouseCaptured() && !input.IsActionPressed("CaptureMouse") &&
            !input.IsActionPressed("MoveForward") && !input.IsActionPressed("pedal0"),
            "Focus return does not turn physically held controls into new presses/capture");
        keys.clear(); middle = left = GLFW_RELEASE; input.Update(0.016f);
        cursor = 150; input.SetMouseCaptured(true);
        input.Shutdown();
        Require(!input.IsMouseCaptured(), "Shutdown releases cursor confinement");
        std::cout << "PASS focus loss, neutralized controls, focus return and capture teardown\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
