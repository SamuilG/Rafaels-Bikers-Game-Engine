#include "InputSystem.hpp"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <string>

namespace engine {
    // =======================================================
    // 銆愭柊澧炪€戯細鐢ㄤ簬鍥炶皟鍑芥暟鐨勫叏灞€鎸囬拡鍜屾棫鍥炶皟瀛樺偍
    static InputSystem* g_InputSystem = nullptr;
    static GLFWscrollfun s_PrevScrollCallback = nullptr;

    // GLFW 婊氳疆鍥炶皟鍑芥暟
    static void ScrollCallback(GLFWwindow* window, double xoffset, double yoffset) {
        // 1. The scroll wheel event is only sent to ImGui if the mouse is not captured by the game
        if (g_InputSystem && !g_InputSystem->IsMouseCaptured()) {
            if (s_PrevScrollCallback) {
                s_PrevScrollCallback(window, xoffset, yoffset);
            }
        }

        // 2. 灏嗘粴杞暟鎹紶閫掔粰鎴戜滑鐨勮緭鍏ョ郴缁?
        if (g_InputSystem) {
            g_InputSystem->AddScrollY(static_cast<float>(yoffset));
        }
    }
    // =======================================================
    void InputSystem::Init() {
        // ==========================================
  
        g_InputSystem = this;
        // ==========================================
        // MUST initialize GLFW so its joystick subsystem is active before we poll or map!
        if (glfwInit() != GLFW_TRUE) {
            printf("[InputSystem] ERROR: glfwInit() failed!\n");
        }

        // Load extensive community SDL mappings for missing Xbox/Linux controller layouts
        std::ifstream file("gamecontrollerdb.txt");
        if (file.is_open()) {
            std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
            if (!content.empty()) {
                if (glfwUpdateGamepadMappings(content.c_str())) {
                    printf("[InputSystem] Successfully updated GLFW gamepad mappings from db.\n");
                }
            }
        } else {
            printf("[InputSystem] WARNING: gamecontrollerdb.txt not found!\n");
        }

        // Enumerate all joysticks for debugging
        //printf("--- Debugging GLFW Joysticks ---\n");
        //for (int i = 0; i <= GLFW_JOYSTICK_LAST; ++i) {
        //    if (glfwJoystickPresent(i)) {
        //        const char* name = glfwGetJoystickName(i);
        //        bool isGamepad = glfwJoystickIsGamepad(i);
        //        const char* guid = glfwGetJoystickGUID(i);
        //        printf("Joystick %d: Name='%s', GUID='%s', IsGamepad=%s\n", 
        //               i, name ? name : "Unknown", guid ? guid : "Unknown", isGamepad ? "TRUE" : "FALSE");
        //        
        //        if (isGamepad) {
        //            const char* gpName = glfwGetGamepadName(i);
        //            printf("  Gamepad Name: '%s'\n", gpName ? gpName : "Unknown");
        //        }
        //    }
        //}
        //printf("--------------------------------\n");

        // Action Mappings


        // --- Movement & Navigation ---
        // Forward/Backward
        MapKeyboardAction("MoveForward", GLFW_KEY_W);
        MapKeyboardAction("MoveForward", GLFW_KEY_UP);
        MapGamepadAxisAction("MoveForward", GLFW_GAMEPAD_AXIS_LEFT_Y, -1.0f);
        
        MapKeyboardAction("MoveBackward", GLFW_KEY_S);
        MapKeyboardAction("MoveBackward", GLFW_KEY_DOWN);
        MapGamepadAxisAction("MoveBackward", GLFW_GAMEPAD_AXIS_LEFT_Y, 1.0f);
      

        // Strafing (Left/Right)
        MapKeyboardAction("StrafeLeft", GLFW_KEY_A);
        MapKeyboardAction("StrafeLeft", GLFW_KEY_LEFT);
        MapGamepadAxisAction("StrafeLeft", GLFW_GAMEPAD_AXIS_LEFT_X, -1.0f);

        MapKeyboardAction("StrafeRight", GLFW_KEY_D);
        MapKeyboardAction("StrafeRight", GLFW_KEY_RIGHT);
        MapGamepadAxisAction("StrafeRight", GLFW_GAMEPAD_AXIS_LEFT_X, 1.0f);
        
        // Vertical Movement (Up/Down)
        MapKeyboardAction("Upward", GLFW_KEY_E);
        MapGamepadButtonAction("Upward", GLFW_GAMEPAD_BUTTON_RIGHT_BUMPER);

        MapKeyboardAction("Downward", GLFW_KEY_Q);
        MapGamepadButtonAction("Downward", GLFW_GAMEPAD_BUTTON_LEFT_BUMPER);

        // Jumping
        MapKeyboardAction("Jump", GLFW_KEY_SPACE);
        MapGamepadButtonAction("Jump", GLFW_GAMEPAD_BUTTON_A);


        // Movement Modifiers (Fast/Slow)
        MapKeyboardAction("Fast", GLFW_KEY_LEFT_SHIFT);
        MapKeyboardAction("Fast", GLFW_KEY_RIGHT_SHIFT);
        MapGamepadAxisAction("Fast", GLFW_GAMEPAD_AXIS_RIGHT_TRIGGER, 1.0f, -0.5f);

        MapKeyboardAction("Slow", GLFW_KEY_LEFT_CONTROL);
        MapKeyboardAction("Slow", GLFW_KEY_RIGHT_CONTROL);
        MapGamepadAxisAction("Slow", GLFW_GAMEPAD_AXIS_LEFT_TRIGGER, 1.0f, -0.5f);
        

        // --- Camera & View ---
        MapKeyboardAction("CameraThirdPersonToggle", GLFW_KEY_T);
        MapGamepadButtonAction("CameraThirdPersonToggle", GLFW_GAMEPAD_BUTTON_Y);
        MapMouseButtonAction("CaptureMouse", GLFW_MOUSE_BUTTON_RIGHT);
        

        // --- Game Systems & Effects ---
        MapKeyboardAction("DEPLOY", GLFW_KEY_R);
        // MapGamepadButtonAction("ToggleParticles", GLFW_GAMEPAD_BUTTON_X);

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


        // ==========================================
        // FOV 
        MapKeyboardAction("ZoomIn", GLFW_KEY_9);  // 9閿細鏀惧ぇ (FOV鍙樺皬)
        MapKeyboardAction("ZoomOut", GLFW_KEY_0); // 0閿細缂╁皬 (FOV鍙樺ぇ)
        // ==========================================

        MapKeyboardAction("BloomToggle", GLFW_KEY_B);
        MapKeyboardAction("IBLToggle", GLFW_KEY_I);
        MapKeyboardAction("SSRToggle", GLFW_KEY_O);
        
        MapKeyboardAction("SSAOToggle", GLFW_KEY_P);
        
		MapKeyboardAction("ToggleEngineUi", GLFW_KEY_F1);// F1閿細鍒囨崲寮曟搸UI鏄剧ず//f1锛?switch engine UI

        // --- Application Control ---
        MapKeyboardAction("Quit", GLFW_KEY_ESCAPE);
        MapGamepadButtonAction("Quit", GLFW_GAMEPAD_BUTTON_BACK);
    }

    void InputSystem::Update(float dt) {

        if (!mWindow) return;
        // ==========================================
        // 鎻愬彇杩欎竴甯х殑婊氳疆鏁版嵁锛屽苟娓呴浂绱姞鍣?
        mScrollDeltaY = mScrollAccumulatorY;
        mScrollAccumulatorY = 0.0f;
        // ==========================================
        // for update window needs to be set

        // Shift current states to previous
        for (auto& pair : mActionBindings) {
            pair.second.wasHeld = pair.second.isHeld;
            pair.second.isHeld = false;
            pair.second.value = 0.0f;
        }

        UpdateKeyboardStates();
        UpdateGamepadStates();
        UpdateMouseStates();
        ApplyInjectedStates();
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
        mGamepadRightX = 0.0;
        mGamepadRightY = 0.0;
        
        int activeGamepad = -1;
        for (int i = 0; i <= GLFW_JOYSTICK_LAST; ++i) {
            if (glfwJoystickPresent(i) && glfwJoystickIsGamepad(i)) {
                activeGamepad = i;
                break;
            }
        }

        if (activeGamepad != -1) {
            GLFWgamepadstate state;
            if (glfwGetGamepadState(activeGamepad, &state)) {
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
                        
                        // Multiply the axis value with our desired direction (-1.0 or 1.0)
                        float directionalValue = val * axisInfo.direction;
                        
                        if (directionalValue > axisInfo.deadzone) {
                            binding.isHeld = true;
                            // Only replace value if this directional magnitude is higher
                            if (directionalValue > binding.value) {
                                binding.value = directionalValue;
                            }
                        }
                    }
                }
                
                // Camera Free Look Extension
                float rawRx = state.axes[GLFW_GAMEPAD_AXIS_RIGHT_X];
                float rawRy = state.axes[GLFW_GAMEPAD_AXIS_RIGHT_Y];
                if (std::abs(rawRx) > 0.1f) mGamepadRightX = rawRx; 
                if (std::abs(rawRy) > 0.1f) mGamepadRightY = rawRy; 
            
            }
        }
    }


    void InputSystem::SetWindow(GLFWwindow* window) {
        mWindow = window;
        if (mWindow) {
            // 璁剧疆鏂扮殑鍥炶皟锛屽苟鎺ョ鏃х殑鍥炶皟锛堥€氬父鏄?ImGui 娉ㄥ唽鐨勶級
            s_PrevScrollCallback = glfwSetScrollCallback(mWindow, ScrollCallback);
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

    void InputSystem::MapGamepadAxisAction(const std::string& actionName, int glfwGamepadAxis, float direction, float deadzone) {
        mActionBindings[actionName].gamepadAxes.push_back({glfwGamepadAxis, direction, deadzone});
    }

    void InputSystem::MapMouseButtonAction(const std::string& actionName, int glfwMouseButton) {
        mActionBindings[actionName].mouseButtons.push_back(glfwMouseButton);
    }

    void InputSystem::ClearInjectedInputs() {
        mInjectedActionValues.clear();
        mInjectedRightStick.reset();
    }

    void InputSystem::InjectActionValue(const std::string& actionName, float value) {
        mInjectedActionValues[actionName] = std::max(0.0f, value);
    }

    void InputSystem::InjectGamepadRightStick(glm::vec2 value) {
        mInjectedRightStick = value;
    }

    void InputSystem::UpdateMouseStates() {
        
        double nx, ny;
        glfwGetCursorPos(mWindow, &nx, &ny);
        
        if (mFirstMouseUpdate) {
            mMouseX = nx; mMouseY = ny;
            mFirstMouseUpdate = false;

        }

        mMouseDeltaX = nx - mMouseX;
        mMouseDeltaY = ny - mMouseY;
        mMouseX = nx;
        mMouseY = ny;

        for (auto& pair : mActionBindings) {
            auto& binding = pair.second;
            for (int btn : binding.mouseButtons) {
                if (glfwGetMouseButton(mWindow, btn) == GLFW_PRESS) {
                    binding.isHeld = true;
                    binding.value = 1.0f;
                    break;
                }
            }
        }



    }

    void InputSystem::SetMouseCaptured(bool captured) {
        if (!mWindow) return;
        glfwSetInputMode(mWindow, GLFW_CURSOR, captured ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
    }

    bool InputSystem::IsMouseCaptured() const {
        if (!mWindow) return false;
        return glfwGetInputMode(mWindow, GLFW_CURSOR) == GLFW_CURSOR_DISABLED;
    }

    void InputSystem::ApplyInjectedStates() {
        for (auto const& [actionName, value] : mInjectedActionValues) {
            if (value <= 0.0f) {
                continue;
            }

            auto& binding = mActionBindings[actionName];
            binding.isHeld = true;
            binding.value = std::max(binding.value, value);
        }

        if (mInjectedRightStick.has_value()) {
            mGamepadRightX = mInjectedRightStick->x;
            mGamepadRightY = mInjectedRightStick->y;
        }
    }

} // namespace engine
