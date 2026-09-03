#pragma once

#include <cstdint>
#include <vector>

#include "../Scene/RenderSnapshot.hpp"
#include "RenderSnapshotGpuData.hpp"

namespace engine::rendering {

    struct StaticInstanceCandidate {
        uint32_t meshIndex = 0;
        uint32_t materialIndex = 0;
        RenderableAssetId renderableAssetId = kInvalidRenderableAssetId;
        uint32_t instanceIndex = kLegacyInstanceIndex;
        bool isTransparent = false;
    };

    struct StaticInstanceDrawGroup {
        uint32_t meshIndex = 0;
        uint32_t materialIndex = 0;
        RenderableAssetId renderableAssetId = kInvalidRenderableAssetId;
        uint32_t firstInstance = 0;
        uint32_t instanceCount = 0;
    };

    inline std::vector<StaticInstanceDrawGroup> BuildStaticInstanceDrawGroups(
        const std::vector<StaticInstanceCandidate>& candidates)
    {
        std::vector<StaticInstanceDrawGroup> groups;
        groups.reserve(candidates.size());

        for (const StaticInstanceCandidate& candidate : candidates) {
            if (candidate.isTransparent || !UsesSnapshotInstance(candidate.instanceIndex)) continue;

            if (!groups.empty()) {
                StaticInstanceDrawGroup& previous = groups.back();
                const bool sameAsset = previous.meshIndex == candidate.meshIndex &&
                    previous.materialIndex == candidate.materialIndex &&
                    previous.renderableAssetId == candidate.renderableAssetId;
                const bool consecutiveInstances =
                    candidate.instanceIndex == previous.firstInstance + previous.instanceCount;
                if (sameAsset && consecutiveInstances) {
                    ++previous.instanceCount;
                    continue;
                }
            }

            groups.push_back({
                candidate.meshIndex,
                candidate.materialIndex,
                candidate.renderableAssetId,
                candidate.instanceIndex,
                1
            });
        }
        return groups;
    }

} // namespace engine::rendering
