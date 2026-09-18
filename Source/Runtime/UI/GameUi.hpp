#pragma once

#include <imgui.h>

namespace engine {
    class RenderSystem;
    struct PlayerState;
    struct EditorState;

    class GameUi {
    public:
        static void ResetTransientState();
        static void DrawHud(RenderSystem* renderSys, const PlayerState& player, const EditorState& editor, const ImVec2& viewportPos, const ImVec2& viewportSize);
    };
} // namespace engine
