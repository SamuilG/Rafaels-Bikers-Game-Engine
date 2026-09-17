// Only platform-facing effects are replaced here. The tests compile the real
// controller, event router, screen manager, serializer, widgets and animations.
#include "Runtime/UI/EngineUi.hpp"
#include "Runtime/UI/VisualUIEditor/ImGuiPreviewRenderer.hpp"
#include "Runtime/AudioSystem/AudioSystem.hpp"

#include <cstdlib>
#include <cstdio>
#include <utility>

namespace engine {
    void EngineUi::PushLogMessage(const std::string&) {}
    void EngineUi::ShowToast(const std::string&) {}

    void ImGuiPreviewRenderer::SetTextureResolver(std::function<void*(const std::string&)> resolver) {
        mTextureResolver = std::move(resolver);
    }

    void ImGuiPreviewRenderer::RenderScreen(const UIScreen&, const UIRenderContext&, UIElementId, UIElementId) {
        std::fputs("FAIL: headless flow tests must not render a frame\n", stderr);
        std::abort();
    }

    // No AudioSystem is constructed or attached by these tests. Fail explicitly
    // if that changes, rather than silently pretending to verify audio behavior.
    float AudioSystem::GetMasterVolume() const { std::abort(); }
    void AudioSystem::SetMasterVolume(float) { std::abort(); }
}
