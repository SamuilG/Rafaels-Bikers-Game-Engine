#pragma once
#include <cstdint>
#include <optional>
#include <array>
#include "FramePerformance.hpp"
#include "ModelResourceStatistics.hpp"

namespace engine {
struct EnvironmentLightingSettings {
    float intensity = 1.0f;
    float diffuseIntensity = 1.0f;
    float specularIntensity = 1.0f;
    std::array<float, 3> tint{1.0f, 1.0f, 1.0f};
    float saturation = 1.0f;
    float rotationDegrees = 0.0f;
    bool ignoreSceneOverride = false;
    bool operator==(const EnvironmentLightingSettings&) const = default;
};
struct ToneMappingSettings {
    bool acesEnabled = true;
    std::array<float, 5> acesCurve{2.51f, 0.03f, 2.43f, 0.59f, 0.14f};
    bool operator==(const ToneMappingSettings&) const = default;
};
struct RenderSettings {
    bool vsyncEnabled = true; // Requests display synchronization; actual mode is a capability.
    uint32_t msaaSamples = 1; // 1 disables MSAA; supported choices are published by the renderer.
    bool taaEnabled = false;
    float taaHistoryWeight = 0.9f;
    bool iblEnabled = true;
    EnvironmentLightingSettings environment;
    ToneMappingSettings toneMapping;
    bool bloomEnabled = true;
    bool ssrEnabled = true;
    bool ssaoEnabled = true;
    bool particlesEnabled = true; // Shared simulation/rendering switch.
    bool frustumCullingEnabled = false; //frustum culling
    float frustumCullingPadding = 0.5f; // new frustum culling
    bool  lodEnabled = true;    // distance-based LOD selection
    bool mosaicEnabled = false; // key 5 toggle
    float bloomExposure = 1.0f;      // 合成阶段曝光（传给 composite shader）。
    float bloomStrength = 1.2f;      // Keep GGX highlights from washing out nearby detail.
    bool operator==(const RenderSettings&) const = default;
};
struct RenderCapabilities {
    uint32_t msaaSampleCounts = 1; // Bit mask of supported sample counts (1, 2, 4, 8).
    uint32_t activeMsaaSamples = 1;
    bool gpuTimestampsSupported = false;
    uint32_t activePresentMode = 2; // VkPresentModeKHR numeric value, without a Vulkan dependency.
    bool vsyncFallback = false; // Requested unlimited presentation is unavailable.
    bool wireframeSupported = false; // Set from the active Vulkan device; read-only in the UI.
};
struct RenderStatistics {
    FramePerformanceStatistics performance;
    ModelResourceStatistics modelResources;
    float frustumCullingOffFps = 0.0f; //off frustum culling fps
    float frustumCullingOnFps = 0.0f; // On frustum culling fps
    uint32_t frustumCullingTotalCandidates = 0; // new frustum culling
    uint32_t frustumCullingVisibleCandidates = 0; // new frustum culling
};
// A level may override its environment without changing the user preference.
struct RenderOverrides {
    std::optional<bool> iblEnabled;
    bool IblEnabled(const RenderSettings& settings) const {
        return settings.environment.ignoreSceneOverride ? settings.iblEnabled : iblEnabled.value_or(settings.iblEnabled);
    }
};
}
