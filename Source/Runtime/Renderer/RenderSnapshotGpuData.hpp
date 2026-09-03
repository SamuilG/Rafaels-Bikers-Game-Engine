#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>

#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>

#include "../Scene/RenderSnapshot.hpp"

namespace engine::rendering
{
    // Matches default.vert's std430 GpuInstanceData layout. This header stays
    // Vulkan-free so the Snapshot-to-GPU conversion can be regression-tested.
    struct alignas(16) GpuInstanceData
    {
        glm::mat4 worldTransform{ 1.0f };
        glm::vec4 opacityAndPadding{ 1.0f, 0.0f, 0.0f, 0.0f };
    };

    static_assert(sizeof(GpuInstanceData) == 80,
                  "GpuInstanceData must match default.vert's std430 layout");

    inline GpuInstanceData MakeGpuInstanceData(const RenderInstance& instance)
    {
        return { instance.worldTransform,
                 glm::vec4(instance.opacity, 0.0f, 0.0f, 0.0f) };
    }

    constexpr std::uint32_t kLegacyInstanceIndex =
        std::numeric_limits<std::uint32_t>::max();

    constexpr bool UsesSnapshotInstance(std::uint32_t instanceIndex)
    {
        return instanceIndex != kLegacyInstanceIndex;
    }

    constexpr std::uint32_t DrawFirstInstance(std::uint32_t instanceIndex)
    {
        return UsesSnapshotInstance(instanceIndex) ? instanceIndex : 0;
    }

    constexpr std::size_t FrameSlot(std::size_t frameIndex, std::size_t framesInFlight)
    {
        return frameIndex % framesInFlight;
    }
}
