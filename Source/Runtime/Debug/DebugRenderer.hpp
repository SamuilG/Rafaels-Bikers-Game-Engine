#pragma once

#include "../Rhi/vkbuffer.hpp"
#include "../Core/System.h"
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <vector>
#include <vulkan/vulkan.h>

namespace engine {

    struct DebugVertex {
        glm::vec3 pos;
        glm::vec3 color;
    };

    class DebugRenderer {
    public:
        DebugRenderer() = default;
        ~DebugRenderer() = default;

        void Init(labut2::Allocator const& allocator, size_t maxVertices = 100000);

        void Shutdown(labut2::Allocator const& allocator);

        void DrawLine(const glm::vec3& start, const glm::vec3& end, const glm::vec3& color = glm::vec3(0.0f, 1.0f, 0.0f));

        void DrawBox(const glm::vec3& center, const glm::vec3& extents, const glm::vec3& color = glm::vec3(1.0f, 0.0f, 0.0f), const glm::mat4& transform = glm::mat4(1.0f));

        void DrawSphere(const glm::vec3& center, float radius, const glm::vec3& color = glm::vec3(0.0f, 0.0f, 1.0f), int segments = 16);

        void DrawCapsule(const glm::vec3& center, float radius, float halfHeight, const glm::vec3& color = glm::vec3(1.0f, 1.0f, 0.0f), int segments = 16);

        void DrawTriangleWireframe(
            const glm::vec3& p0,
            const glm::vec3& p1,
            const glm::vec3& p2,
            const glm::vec3& color = glm::vec3(0.0f, 1.0f, 0.0f)
        ) {
            DrawLine(p0, p1, color);
            DrawLine(p1, p2, color);
            DrawLine(p2, p0, color);
        }

        void Upload(labut2::Allocator const& allocator);

        void Render(VkCommandBuffer cmdBuff, VkPipeline debugPipeline, VkPipelineLayout layout, VkDescriptorSet sceneDesc);

        void Clear();

    private:
        std::vector<DebugVertex> mVertices;
        labut2::Buffer           mVertexBuffer;
        size_t                   mMaxVertices = 0;
    };

} // namespace engine