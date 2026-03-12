#pragma once

#include "../Core/System.h"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <string>
#include <unordered_map>
#include <vector>

namespace engine {

    class InputSystem final : public System {
    public:
        InputSystem() = default;

        
        void Init() override;
        void Update(float dt) override;
        void Shutdown() override;

        // Call to provide the GLFW window pointer once created by the Window/Render system
        void SetWindow(GLFWwindow* window) { mWindow = window; }

        // Core Input Polling
        // checks if an action was JUST pressed this frame
        bool IsActionPressed(const std::string& actionName) const;
        // checks if an action is CURRENTLY being held down
        bool IsActionHeld(const std::string& actionName) const;
        // gets an analog value for triggers or axes mapped to this action (1.0 for digital keys)
        float GetActionValue(const std::string& actionName) const;



        // Mapping Setup
        // map a keyboard mapping to an action
        void MapKeyboardAction(const std::string& actionName, int glfwKeyCode);
        // map a gamepad button to an action
        void MapGamepadButtonAction(const std::string& actionName, int glfwGamepadButton);
        // map a gamepad axis to an action
        void MapGamepadAxisAction(const std::string& actionName, int glfwGamepadAxis, float deadzone = 0.1f);


    private:
        GLFWwindow* mWindow = nullptr;

        // store bindings for various devices
        struct ActionBinding {
            std::vector<int> keyboardKeys;
            std::vector<int> gamepadButtons;
            struct AxisInfo { int axis; float deadzone; };
            std::vector<AxisInfo> gamepadAxes;
            
            // state tracking
            bool isHeld = false;
            bool wasHeld = false;
            float value = 0.0f;
        };


        std::unordered_map<std::string, ActionBinding> mActionBindings;

        void UpdateKeyboardStates();
        void UpdateGamepadStates();




    };

} // namespace engine
