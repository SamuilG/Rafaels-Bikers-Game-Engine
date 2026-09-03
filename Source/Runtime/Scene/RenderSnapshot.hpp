#pragma once

#include <cstdint>
#include <limits>
#include <vector>

#include <glm/mat4x4.hpp>

// A renderable asset identifies one complete mesh/material unit.
// The renderer resolves it to its GPU representation.
using RenderableAssetId = std::uint32_t;
constexpr RenderableAssetId kInvalidRenderableAssetId =
    std::numeric_limits<RenderableAssetId>::max();

struct RenderInstance
{
    RenderableAssetId renderableAssetId = kInvalidRenderableAssetId;
    glm::mat4 worldTransform{1.0f};
    float opacity = 1.0f;
    bool castShadow = true;
};

using SkinInstanceId = std::uint32_t;
constexpr SkinInstanceId kInvalidSkinInstanceId =
    std::numeric_limits<SkinInstanceId>::max();

// A skinned instance references its frame-local palette. The palette itself
// lives in AnimationRenderData, so it is not duplicated per render instance.
struct SkinnedRenderInstance
{
    RenderableAssetId renderableAssetId = kInvalidRenderableAssetId;
    glm::mat4 worldTransform{1.0f};
    float opacity = 1.0f;
    bool castShadow = true;
    SkinInstanceId skinInstanceId = kInvalidSkinInstanceId;
};

struct AnimationRenderData
{
    std::vector<std::vector<glm::mat4>> skinPalettes;
};

struct CameraView
{
    glm::mat4 worldTransform{1.0f};
    glm::mat4 view{1.0f};
    glm::mat4 projection{1.0f};
    glm::mat4 projView{1.0f};
    glm::vec3 worldPosition{0.0f};
};

struct RenderSnapshot
{
    std::vector<RenderInstance> instances;
    std::vector<SkinnedRenderInstance> skinnedInstances;
    CameraView camera;
    bool hasCamera = false;
};
