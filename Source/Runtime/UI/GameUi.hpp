#pragma once

#include <imgui.h>

namespace engine {
    class RenderSystem;
    struct UserState;

    class GameUi {
    public:
        static void DrawHud(RenderSystem* renderSys, const UserState& state, const ImVec2& viewportPos, const ImVec2& viewportSize);
    };
} // namespace engine
