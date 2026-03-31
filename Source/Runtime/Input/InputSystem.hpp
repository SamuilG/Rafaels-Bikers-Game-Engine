#pragma once

#include "../Core/System.h"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <string>
#include <unordered_map>
#include <vector>
#include <glm/glm.hpp>

namespace engine {

    class InputSystem final : public System {
    public:
        InputSystem() = default;

        
        void Init() override;
        void Update(float dt) override;
        void Shutdown() override;

        // Call to provide the GLFW window pointer once created by the Window/Render system
        void SetWindow(GLFWwindow* window);
        // Core Input Polling
        // checks if an action was JUST pressed this frame
        bool IsActionPressed(const std::string& actionName) const;
        // checks if an action is CURRENTLY being held down
        bool IsActionHeld(const std::string& actionName) const;
        // gets an analog value for triggers or axes mapped to this action (1.0 for digital keys)
        float GetActionValue(const std::string& actionName) const;

        // Mouse specific
        glm::vec2 GetMouseDelta() const { return glm::vec2(static_cast<float>(mMouseDeltaX), static_cast<float>(mMouseDeltaY)); }
        glm::vec2 GetMousePosition() const { return glm::vec2(static_cast<float>(mMouseX), static_cast<float>(mMouseY)); }
        void SetMouseCaptured(bool captured);
        bool IsMouseCaptured() const;

        // Gamepad specific
        glm::vec2 GetGamepadRightStick() const { return glm::vec2(static_cast<float>(mGamepadRightX), static_cast<float>(mGamepadRightY)); }



        // Mapping Setup
        // map a keyboard mapping to an action
        void MapKeyboardAction(const std::string& actionName, int glfwKeyCode);
        // map a gamepad button to an action
        void MapGamepadButtonAction(const std::string& actionName, int glfwGamepadButton);
        // map a gamepad axis to an action
        void MapGamepadAxisAction(const std::string& actionName, int glfwGamepadAxis, float direction = 1.0f, float deadzone = 0.1f);
        // map a mouse button to an action
        void MapMouseButtonAction(const std::string& actionName, int glfwMouseButton);

        // 获取当前帧的滚轮滚动量
        float GetScrollY() const { return mScrollDeltaY; }

        // 给回调函数使用的累加器
        void AddScrollY(float yOffset) { mScrollAccumulatorY += yOffset; }

        // 原本就有的 SetWindow，如果没有请确保它的存在
       // void SetWindow(GLFWwindow* window);

    private:
        GLFWwindow* mWindow = nullptr;

        // store bindings for various devices
        struct ActionBinding {
            std::vector<int> keyboardKeys;
            std::vector<int> gamepadButtons;
            struct AxisInfo { int axis; float direction; float deadzone; };
            std::vector<AxisInfo> gamepadAxes;
            std::vector<int> mouseButtons;
            
            // state tracking
            bool isHeld = false;
            bool wasHeld = false;
            float value = 0.0f;
        };


        std::unordered_map<std::string, ActionBinding> mActionBindings;

        double mMouseX = 0.0;
        double mMouseY = 0.0;
        double mMouseDeltaX = 0.0;
        double mMouseDeltaY = 0.0;
        bool mFirstMouseUpdate = true;

        double mGamepadRightX = 0.0;
        double mGamepadRightY = 0.0;

        void UpdateKeyboardStates();
        void UpdateGamepadStates();
        void UpdateMouseStates();


        float mScrollDeltaY = 0.0f;       // 当前帧可用的滚轮变化量
        float mScrollAccumulatorY = 0.0f; // 累加器（因为回调在帧中间随时可能触发）

    };

} // namespace engine
