#pragma once

#include <algorithm>
#include <optional>

#include "RenderSnapshot.hpp"

namespace engine::scene_render_extraction
{
    constexpr float kInvisibleOpacityThreshold = 0.001f;

    inline std::optional<RenderInstance> MakeRenderInstance(
        RenderableAssetId assetId, const glm::mat4& worldTransform,
        float opacity, bool castShadow)
    {
        if (assetId == kInvalidRenderableAssetId) return std::nullopt;
        const float clampedOpacity = std::clamp(opacity, 0.0f, 1.0f);
        if (clampedOpacity <= kInvisibleOpacityThreshold) return std::nullopt;
        return RenderInstance{ assetId, worldTransform, clampedOpacity, castShadow };
    }

    inline std::optional<SkinnedRenderInstance> MakeSkinnedRenderInstance(
        RenderableAssetId assetId, const glm::mat4& worldTransform,
        float opacity, bool castShadow, SkinInstanceId skinInstanceId,
        bool hasSkinPalette)
    {
        if (!hasSkinPalette || assetId == kInvalidRenderableAssetId) return std::nullopt;
        const float clampedOpacity = std::clamp(opacity, 0.0f, 1.0f);
        if (clampedOpacity <= kInvisibleOpacityThreshold) return std::nullopt;
        return SkinnedRenderInstance{ assetId, worldTransform, clampedOpacity,
                                      castShadow, skinInstanceId };
    }
}
