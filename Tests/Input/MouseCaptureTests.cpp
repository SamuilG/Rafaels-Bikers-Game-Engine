#include "Runtime/Input/InputSystem.hpp"
#include <iostream>
#include <stdexcept>

namespace {
int middle = GLFW_RELEASE;
int mode = GLFW_CURSOR_NORMAL;
int changes = 0;
double cursor = 0;
void Require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
}
extern "C" {
int glfwInit() { return GLFW_TRUE; }
int glfwUpdateGamepadMappings(const char*) { return GLFW_TRUE; }
void glfwPollEvents() {}
int glfwGetKey(GLFWwindow*, int) { return GLFW_RELEASE; }
int glfwJoystickPresent(int) { return GLFW_FALSE; }
int glfwJoystickIsGamepad(int) { return GLFW_FALSE; }
int glfwGetGamepadState(int, GLFWgamepadstate*) { return GLFW_FALSE; }
GLFWscrollfun glfwSetScrollCallback(GLFWwindow*, GLFWscrollfun) { return nullptr; }
void glfwGetCursorPos(GLFWwindow*, double* x, double* y) { *x = cursor; *y = cursor; }
int glfwGetMouseButton(GLFWwindow*, int button) { return button == GLFW_MOUSE_BUTTON_MIDDLE ? middle : GLFW_RELEASE; }
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
        input.Shutdown();
        std::cout << "PASS middle-button edges, capture persistence and cursor warp suppression\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
