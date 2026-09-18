#pragma once
#include <cstdint>
#include <optional>

namespace engine {
struct RenderSettings {
    bool iblEnabled = true;
    bool bloomEnabled = true;
    bool ssrEnabled = true;
    bool ssaoEnabled = true;
    bool particlesEnabled = true; // Shared simulation/rendering switch.
    bool frustumCullingEnabled = false; //frustum culling
    float frustumCullingPadding = 0.5f; // new frustum culling
    bool  lodEnabled = true;    // distance-based LOD selection
    bool mosaicEnabled = false; // key 5 toggle
    float bloomExposure = 1.0f;      // 合成阶段曝光（传给 composite shader）
    float bloomStrength = 2.2f;      // Preserve the renderer's original composite strength.
};
struct RenderCapabilities {
    bool wireframeSupported = false; // Set from the active Vulkan device; read-only in the UI.
};
struct RenderStatistics {
    float frustumCullingOffFps = 0.0f; //off frustum culling fps
    float frustumCullingOnFps = 0.0f; // On frustum culling fps
    uint32_t frustumCullingTotalCandidates = 0; // new frustum culling
    uint32_t frustumCullingVisibleCandidates = 0; // new frustum culling
};
// A level may override its environment without changing the user preference.
struct RenderOverrides {
    std::optional<bool> iblEnabled;
    bool IblEnabled(const RenderSettings& settings) const { return iblEnabled.value_or(settings.iblEnabled); }
};
}
