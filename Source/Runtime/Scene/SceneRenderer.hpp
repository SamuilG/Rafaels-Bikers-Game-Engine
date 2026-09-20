#pragma once

#include "SceneManager.hpp"

namespace engine {

// CPU-side scene render preparation. It deliberately owns no Vulkan objects;
// RenderSystem remains responsible for frame resources and command recording.
class SceneRenderer final {
public:
    explicit SceneRenderer(SceneRenderSource* source = nullptr) : mSource(source) {}

    void SetSource(SceneRenderSource* source) { mSource = source; }
    SceneRenderSource* Source() const { return mSource; }

    std::vector<::RenderBatch> BuildOpaque(const SceneRenderRequest& request) const {
        return mSource ? mSource->BuildRenderBatches(request) : std::vector<::RenderBatch>{};
    }

    std::vector<::RenderBatch> BuildSkinned(glm::mat4* boneBuffer,
        std::size_t maxBones, std::size_t& outBoneCount) const {
        outBoneCount = 0;
        return mSource ? mSource->BuildSkinnedBatches(boneBuffer, maxBones, outBoneCount)
            : std::vector<::RenderBatch>{};
    }

    std::vector<GpuLight> CollectLights() const {
        std::vector<GpuLight> lights;
        if (mSource) mSource->CollectLights(lights);
        return lights;
    }

    std::uint32_t LastFrustumCandidates() const {
        return mSource ? mSource->LastFrustumCandidates() : 0;
    }

    std::uint32_t LastFrustumVisible() const {
        return mSource ? mSource->LastFrustumVisible() : 0;
    }

private:
    SceneRenderSource* mSource = nullptr;
};

} // namespace engine
