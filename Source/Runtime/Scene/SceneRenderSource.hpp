#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>
#include <glm/glm.hpp>

struct RenderBatch;

namespace engine {
struct GpuLight;
struct Frustum;

// Read-only render boundary exposed by a scene world. RenderSystem owns the
// GPU frame and consumes this boundary; it does not need to know how ECS
// entities, physics bodies or editor selection are stored.
struct SceneRenderRequest {
    const Frustum* frustum = nullptr;
    float frustumPadding = 0.0f;
    glm::vec3 cameraPosition = glm::vec3(0.0f);
};

class SceneRenderSource {
public:
    virtual ~SceneRenderSource() = default;

    virtual std::vector<::RenderBatch> BuildRenderBatches(const SceneRenderRequest& request) = 0;
    virtual std::vector<::RenderBatch> BuildSkinnedBatches(glm::mat4* boneBuffer,
        std::size_t maxBones, std::size_t& outBoneCount) = 0;
    virtual void CollectLights(std::vector<GpuLight>& outLights) = 0;
    virtual std::uint32_t LastFrustumCandidates() const = 0;
    virtual std::uint32_t LastFrustumVisible() const = 0;
};

} // namespace engine
