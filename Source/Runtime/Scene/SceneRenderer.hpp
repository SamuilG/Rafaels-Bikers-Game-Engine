#pragma once

#include "RenderBatch.hpp"
#include "SceneRenderSource.hpp"
#include "../Renderer/RenderUtilities/light.hpp"
#include "../Renderer/RenderUtilities/SceneEffects.hpp"
#include <algorithm>
#include <limits>

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
    DirectionalLightState GetDirectionalLight() const {
        return mSource ? mSource->GetDirectionalLight() : DirectionalLightState{};
    }

    std::uint32_t LastFrustumCandidates() const {
        return mSource ? mSource->LastFrustumCandidates() : 0;
    }

    std::uint32_t LastFrustumVisible() const {
        return mSource ? mSource->LastFrustumVisible() : 0;
    }

    // Keep the crossing object's source geometry even when its real position is
    // culled: its mapped copy may be visible on the other side of the portal.
    std::vector<::RenderBatch> PortalCloneSources(const PortalTransitionVisual& visual,
        const glm::vec3& cameraPosition) const {
        std::vector<::RenderBatch> sources;
        if (!visual.active || visual.bodyId == std::numeric_limits<std::uint32_t>::max()) return sources;
        for (const auto& batch : BuildOpaque({nullptr, 0.0f, cameraPosition})) {
            if (!batch.isPortalClone && batch.compoundBodyID == visual.bodyId) sources.push_back(batch);
        }
        return sources;
    }

    static void ApplyOpaquePortalTransition(std::vector<::RenderBatch>& batches,
        const PortalTransitionVisual& visual, const std::vector<::RenderBatch>& unculledSources) {
        if (!visual.active || visual.bodyId == std::numeric_limits<std::uint32_t>::max()) return;
        auto matches = [&](const ::RenderBatch& batch) { return batch.compoundBodyID == visual.bodyId; };
        std::vector<::RenderBatch> sources;
        for (const auto& source : unculledSources) {
            if (!source.isPortalClone && matches(source)) sources.push_back(source);
        }
        ApplyTransition(batches, visual, matches, sources);
    }

    static void ApplySkinnedPortalTransition(std::vector<::RenderBatch>& batches,
        const PortalTransitionVisual& visual) {
        if (!visual.active || visual.entityId == 0) return;
        auto matches = [&](const ::RenderBatch& batch) { return batch.attachedEntityId == visual.entityId; };
        ApplyTransition(batches, visual, matches, {});
    }

private:
    template<typename Predicate>
    static void ApplyTransition(std::vector<::RenderBatch>& batches,
        const PortalTransitionVisual& visual, Predicate matches,
        std::vector<::RenderBatch> sources) {
        // Reapplying to a cached main/portal pass replaces existing target
        // clones. Non-target geometry, including other clones, stays untouched.
        batches.erase(std::remove_if(batches.begin(), batches.end(),
            [&](const ::RenderBatch& batch) { return batch.isPortalClone && matches(batch); }), batches.end());
        if (sources.empty()) {
            for (const auto& batch : batches) {
                if (!batch.isPortalClone && matches(batch)) sources.push_back(batch);
            }
        }
        for (auto& batch : batches) {
            if (!batch.isPortalClone && matches(batch)) batch.clipPlane = visual.realClipPlane;
        }
        for (const auto& source : sources) {
            ::RenderBatch clone = source;
            clone.transform = visual.cloneMap * source.transform;
            clone.clipPlane = visual.cloneClipPlane;
            clone.castShadow = false;
            clone.isPortalClone = true;
            batches.push_back(clone);
        }
    }

    SceneRenderSource* mSource = nullptr;
};

} // namespace engine
