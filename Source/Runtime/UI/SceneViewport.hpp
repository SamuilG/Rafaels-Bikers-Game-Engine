#pragma once

#include <imgui.h>

namespace engine {

// The runtime canvas belongs to the host, independent of any game's HUD.
class SceneViewport final {
public:
    static void Set(const ImVec2& position, const ImVec2& size, ImDrawList* drawList) {
        sPosition = position;
        sSize = size;
        sDrawList = drawList;
    }

    static void Reset() {
        sPosition = ImVec2(0.0f, 0.0f);
        sSize = ImVec2(1280.0f, 720.0f);
        sDrawList = nullptr;
    }

    static ImVec2 GetPos() { return sPosition; }
    static ImVec2 GetSize() { return sSize; }
    static ImDrawList* GetDrawList() { return sDrawList; }

private:
    inline static ImVec2 sPosition{0.0f, 0.0f};
    inline static ImVec2 sSize{1280.0f, 720.0f};
    inline static ImDrawList* sDrawList = nullptr;
};

} // namespace engine
