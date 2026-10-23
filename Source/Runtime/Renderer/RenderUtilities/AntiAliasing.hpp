#pragma once
#include "../../Rhi/vkimage.hpp"
#include "../../Rhi/vkbuffer.hpp"
#include "../../Rhi/vkobject.hpp"
#include "../../Scene/RenderBatch.hpp"
#include "../../UserState/RenderSettings.hpp"
#include <glm/glm.hpp>
#include <array>
#include <map>
#include <tuple>
#include <unordered_map>
#include <vector>

namespace engine {

struct MsaaSupport {
    uint32_t sampleCounts = 1;
    VkResolveModeFlagBits depthResolve = VK_RESOLVE_MODE_NONE;
};
MsaaSupport QueryMsaaSupport(VkPhysicalDevice device);
uint32_t SelectMsaaSamples(uint32_t requested, uint32_t supported);
glm::vec2 TemporalJitter(uint32_t frame); // Halton (2,3), in pixel units.

// Shared scratch attachments: all portal and main passes execute sequentially
// on the graphics queue and resolve to their own single-sample destinations.
struct MsaaTargets {
    VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT;
    VkResolveModeFlagBits depthResolve = VK_RESOLVE_MODE_NONE;
    std::array<labut2::ImageWithView, 3> colors;
    labut2::ImageWithView depth;
    void Configure(const labut2::VulkanContext&, const labut2::Allocator&, VkExtent2D,
        uint32_t count, VkResolveModeFlagBits);
    void Attach(VkCommandBuffer, VkRenderingAttachmentInfo* colors,
        VkRenderingAttachmentInfo& depth) const;
};

struct TemporalGeometry {
    const std::vector<labut2::Buffer>& positions;
    const std::vector<labut2::Buffer>& indices;
    const std::vector<uint32_t>& indexCounts;
    const std::unordered_map<uint32_t, labut2::Buffer>& joints;
    const std::unordered_map<uint32_t, labut2::Buffer>& weights;
};

// A separate visibility-tested motion pass keeps the existing material/shadow
// descriptor and 128-byte push-constant ABI intact. All CPU-written buffers are
// per frame slot and reused only after that slot's existing fence completes.
class TemporalAntiAliasing {
public:
    void Initialize(const labut2::VulkanContext&, const char* shaderDirectory = "Assets/Shaders/spirv");
    void Resize(const labut2::VulkanContext&, const labut2::Allocator&, VkExtent2D, size_t frameSlots);
    void ResetHistory();
    void Shutdown(); // Caller has completed outstanding graphics submissions.
    bool BeginFrame(const RenderSettings&, const glm::mat4& view, const glm::mat4& projection,
        glm::vec2 viewportSize, const void* scene, bool enabled);
    void Prepare(const labut2::VulkanContext&, const labut2::Allocator&, size_t slot,
        const glm::mat4& jitteredViewProjection, const glm::mat4& jitteredProjection,
        const std::vector<RenderBatch>& opaque, const std::vector<RenderBatch>& skinned,
        const std::vector<glm::mat4>& bones, VkImageView currentColor, VkImageView depth);
    // Input color is COLOR_ATTACHMENT_OPTIMAL and supports transfer destination.
    // Depth is DEPTH_STENCIL_READ_ONLY_OPTIMAL. Restores the input color layout.
    // History is saved before particles, debug overlays and camera effects.
    void Resolve(VkCommandBuffer, size_t slot, VkImage color, const TemporalGeometry&);
    void Submitted();
    glm::vec2 Jitter() const { return mJitter; }
    bool HistoryValid() const { return mHistoryValid; }
    uint32_t FrameNumber() const { return mFrameNumber; }

private:
    struct alignas(16) Uniform {
        glm::mat4 currentViewProjection, previousViewProjection, inverseProjection;
        glm::vec4 parameters; // history weight, valid history, width, height
        glm::mat4 inverseViewProjection;
        glm::vec4 currentCameraPosition, previousCameraPosition;
    };
    struct alignas(16) Object {
        glm::mat4 currentModel, previousModel;
        glm::uvec4 bones; // current base, previous base, skinned, valid previous geometry
    };
    using Key = std::tuple<uint64_t, uint32_t, bool>;
    struct PreviousObject { glm::mat4 model; uint32_t boneBase; };
    struct Draw { uint32_t mesh, object; bool skinned; };
    struct Frame {
        labut2::Buffer uniform, objects, bones;
        size_t objectCapacity = 0, boneCapacity = 0;
        VkDescriptorSet motionSet{}, resolveSets[2]{};
        std::vector<Draw> draws;
    };
    labut2::DescriptorSetLayout mMotionSetLayout, mResolveSetLayout;
    labut2::PipelineLayout mMotionLayout, mResolveLayout;
    labut2::Pipeline mMotionPipe, mSkinnedMotionPipe, mResolvePipe;
    labut2::DescriptorPool mPool;
    labut2::Sampler mSampler, mDepthSampler;
    std::array<labut2::ImageWithView, 2> mHistory;
    labut2::ImageWithView mMotion;
    std::vector<Frame> mFrames;
    std::array<bool, 2> mHistoryInitialized{};
    VkExtent2D mExtent{};
    std::map<Key, PreviousObject> mPreviousObjects, mPendingObjects;
    std::vector<glm::mat4> mPreviousBones, mPendingBones;
    glm::mat4 mPreviousViewProjection{1}, mPendingViewProjection{1};
    glm::vec4 mPreviousCameraPosition{};
    glm::mat4 mLastView{1}, mLastProjection{1};
    glm::vec2 mLastViewport{}, mJitter{};
    RenderSettings mLastSettings;
    const void* mLastScene = nullptr;
    uint32_t mFrameNumber = 0, mReadHistory = 0;
    bool mHistoryValid = false, mEnabled = false, mHaveCamera = false;
};
struct AntiAliasingFrame {
    const MsaaTargets* msaa = nullptr;
    TemporalAntiAliasing* temporal = nullptr;
    size_t slot = 0;
    const TemporalGeometry* geometry = nullptr;
};
} // namespace engine
