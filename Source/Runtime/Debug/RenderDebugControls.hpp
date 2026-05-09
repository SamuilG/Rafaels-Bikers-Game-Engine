#pragma once

namespace engine {
    class InputSystem;
    class UserState;

    class RenderDebugControls {
    public:
        static void HandleUiDebugHotkeys(UserState& state);
        static void HandleRenderDebugActions(UserState& state, InputSystem& inputSystem);
    };
} // namespace engine
