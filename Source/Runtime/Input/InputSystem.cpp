#include "InputSystem.hpp"
#include <algorithm>
#include <cmath>

namespace engine {

    void InputSystem::Init() {
        // Action Mappings
        // linking abstract gameplay actions to physical 
        // hardware inputs: keyboard/mouse (gamepad to some extend lol)


        // --- Movement & Navigation ---
        // Forward/Backward
        MapKeyboardAction("MoveForward", GLFW_KEY_W);
        MapKeyboardAction("MoveForward", GLFW_KEY_UP);
        MapGamepadAxisAction("MoveForward", GLFW_GAMEPAD_AXIS_LEFT_Y);
        // Note: Left Y is intuitively mapped here; deadzone processing handles directional separation

        MapKeyboardAction("MoveBackward", GLFW_KEY_S);
        MapKeyboardAction("MoveBackward", GLFW_KEY_DOWN);


        // Strafing (Left/Right)
        MapKeyboardAction("StrafeLeft", GLFW_KEY_A);
        MapKeyboardAction("StrafeLeft", GLFW_KEY_LEFT);
        MapGamepadAxisAction("StrafeLeft", GLFW_GAMEPAD_AXIS_LEFT_X);

        MapKeyboardAction("StrafeRight", GLFW_KEY_D);
        MapKeyboardAction("StrafeRight", GLFW_KEY_RIGHT);
        
        // Vertical Movement (Up/Down)
        MapKeyboardAction("Upward", GLFW_KEY_E);
        MapKeyboardAction("Downward", GLFW_KEY_Q);


        // Movement Modifiers (Fast/Slow)
        MapKeyboardAction("Fast", GLFW_KEY_LEFT_SHIFT);
        MapKeyboardAction("Fast", GLFW_KEY_RIGHT_SHIFT);
        MapGamepadButtonAction("Fast", GLFW_GAMEPAD_BUTTON_LEFT_BUMPER);

        MapKeyboardAction("Slow", GLFW_KEY_LEFT_CONTROL);
        MapKeyboardAction("Slow", GLFW_KEY_RIGHT_CONTROL);
        

        // --- Camera & View ---
        MapKeyboardAction("CameraThirdPersonToggle", GLFW_KEY_T);
        MapGamepadButtonAction("CameraThirdPersonToggle", GLFW_GAMEPAD_BUTTON_Y);
        

        // --- Game Systems & Effects ---
        MapKeyboardAction("ToggleParticles", GLFW_KEY_R);
        MapGamepadButtonAction("ToggleParticles", GLFW_GAMEPAD_BUTTON_X);

        // --- Debug Toggles ---
        MapKeyboardAction("Default", GLFW_KEY_1); // Default
        MapKeyboardAction("DebugMipmap", GLFW_KEY_2); // Mipmap
        MapKeyboardAction("DebugDepth", GLFW_KEY_3); // Depth
        MapKeyboardAction("DebugDerivatives", GLFW_KEY_4); // Derivatives
        MapKeyboardAction("DebugMosaic", GLFW_KEY_5); // Mosaic Toggle
        MapKeyboardAction("DebugOverdraw", GLFW_KEY_6); // Overdraw
        MapKeyboardAction("DebugOvershading", GLFW_KEY_7); // Overshading
        MapKeyboardAction("DebugShadows", GLFW_KEY_8);; // Shadow Debug
        MapKeyboardAction("PrintCameraPos", GLFW_KEY_P); // Print Camera Position

        // --- Application Control ---
        MapKeyboardAction("Quit", GLFW_KEY_ESCAPE);
        MapGamepadButtonAction("Quit", GLFW_GAMEPAD_BUTTON_BACK);
    }

    void InputSystem::Update(float dt) {
        if (!mWindow) return;
        // for update window needs to be set

        // Shift current states to previous
        for (auto& pair : mActionBindings) {
            pair.second.wasHeld = pair.second.isHeld;
            pair.second.isHeld = false;
            pair.second.value = 0.0f;
        }

        UpdateKeyboardStates();
        UpdateGamepadStates();
    }

    void InputSystem::Shutdown() {
        mActionBindings.clear();
    }

    void InputSystem::UpdateKeyboardStates() {
        for (auto& pair : mActionBindings) {
            auto& binding = pair.second;
            
            for (int key : binding.keyboardKeys) {
                if (glfwGetKey(mWindow, key) == GLFW_PRESS) {
                    binding.isHeld = true;
                    binding.value = 1.0f;
                    break;
                }
            }
        }
    }

    void InputSystem::UpdateGamepadStates() {
        GLFWgamepadstate state;
        if (glfwGetGamepadState(GLFW_JOYSTICK_1, &state)) {
            for (auto& pair : mActionBindings) {
                auto& binding = pair.second;

                // Buttons
                for (int btn : binding.gamepadButtons) {
                    if (state.buttons[btn] == GLFW_PRESS) {
                        binding.isHeld = true;
                        binding.value = 1.0f;
                    }
                }

                // Axes
                for (const auto& axisInfo : binding.gamepadAxes) {
                    float val = state.axes[axisInfo.axis];
                    
                    // Specific mapping logic:
                    // Left Y is negative going UP for GLFW
                    // take absolute for the magnitude, and the game logic handles directions
                    
                    // for directional axes split, we should separate them or return negative
                    if (std::abs(val) > axisInfo.deadzone) {
                        binding.isHeld = true;
                        // replace value if magnitude is higher
                        if (std::abs(val) > std::abs(binding.value)) {
                            binding.value = val;
                        }
                    }
                }
            }
        }
    }

    bool InputSystem::IsActionPressed(const std::string& actionName) const {
        auto it = mActionBindings.find(actionName);
        if (it != mActionBindings.end()) {
            return it->second.isHeld && !it->second.wasHeld;
        }
        return false;
    }

    bool InputSystem::IsActionHeld(const std::string& actionName) const {
        auto it = mActionBindings.find(actionName);
        if (it != mActionBindings.end()) {
            return it->second.isHeld;
        }
        return false;
    }

    float InputSystem::GetActionValue(const std::string& actionName) const {
        auto it = mActionBindings.find(actionName);
        if (it != mActionBindings.end()) {
            return it->second.value;
        }
        return 0.0f;
    }

    void InputSystem::MapKeyboardAction(const std::string& actionName, int glfwKeyCode) {
        mActionBindings[actionName].keyboardKeys.push_back(glfwKeyCode);
    }

    void InputSystem::MapGamepadButtonAction(const std::string& actionName, int glfwGamepadButton) {
        mActionBindings[actionName].gamepadButtons.push_back(glfwGamepadButton);
    }

    void InputSystem::MapGamepadAxisAction(const std::string& actionName, int glfwGamepadAxis, float deadzone) {
        mActionBindings[actionName].gamepadAxes.push_back({glfwGamepadAxis, deadzone});
    }

} // namespace engine
