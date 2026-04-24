#include "DebugRenderer.hpp"
#include <cstring> 

namespace engine {

    void DebugRenderer::Init(labut2::Allocator const& allocator, size_t maxVertices) {
        mMaxVertices = maxVertices;
        mVertices.reserve(mMaxVertices);

        
        mVertexBuffer = labut2::create_buffer(allocator,
            mMaxVertices * sizeof(DebugVertex),
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT);
    }

    void DebugRenderer::Shutdown(labut2::Allocator const& allocator) {
        mVertexBuffer = labut2::Buffer();
        mVertices.clear();
    }

    void DebugRenderer::DrawLine(const glm::vec3& start, const glm::vec3& end, const glm::vec3& color) {
        if (mVertices.size() + 2 > mMaxVertices) return; 
        mVertices.push_back({ start, color });
        mVertices.push_back({ end, color });
    }

    void DebugRenderer::DrawBox(const glm::vec3& center, const glm::vec3& extents, const glm::vec3& color, const glm::mat4& transform) {
        glm::vec3 points[8];
        int i = 0;
        for (float x : {-1.0f, 1.0f}) {
            for (float y : {-1.0f, 1.0f}) {
                for (float z : {-1.0f, 1.0f}) {
                    glm::vec4 p = transform * glm::vec4(center + extents * glm::vec3(x, y, z), 1.0f);
                    points[i++] = glm::vec3(p);
                }
            }
        }
        // 底面
        DrawLine(points[0], points[1], color); DrawLine(points[1], points[3], color);
        DrawLine(points[3], points[2], color); DrawLine(points[2], points[0], color);
        // 顶面
        DrawLine(points[4], points[5], color); DrawLine(points[5], points[7], color);
        DrawLine(points[7], points[6], color); DrawLine(points[6], points[4], color);
        // 侧边
        DrawLine(points[0], points[4], color); DrawLine(points[1], points[5], color);
        DrawLine(points[2], points[6], color); DrawLine(points[3], points[7], color);
    }

    void DebugRenderer::DrawSphere(const glm::vec3& center, float radius, const glm::vec3& color, int segments) {
        float angleStep = glm::two_pi<float>() / segments;
        for (int i = 0; i < segments; ++i) {
            float a1 = i * angleStep;
            float a2 = (i + 1) * angleStep;
            // XY平面
            DrawLine(center + radius * glm::vec3(cos(a1), sin(a1), 0), center + radius * glm::vec3(cos(a2), sin(a2), 0), color);
            // XZ平面
            DrawLine(center + radius * glm::vec3(cos(a1), 0, sin(a1)), center + radius * glm::vec3(cos(a2), 0, sin(a2)), color);
            // YZ平面
            DrawLine(center + radius * glm::vec3(0, cos(a1), sin(a1)), center + radius * glm::vec3(0, cos(a2), sin(a2)), color);
        }
    }

    void DebugRenderer::DrawCapsule(const glm::vec3& center, float radius, float halfHeight, const glm::vec3& color, int segments) {
        glm::vec3 topCenter = center + glm::vec3(0, halfHeight, 0);
        glm::vec3 bottomCenter = center - glm::vec3(0, halfHeight, 0);

        DrawSphere(topCenter, radius, color, segments);
        DrawSphere(bottomCenter, radius, color, segments);

        // 身体的4条垂线
        DrawLine(topCenter + glm::vec3(radius, 0, 0), bottomCenter + glm::vec3(radius, 0, 0), color);
        DrawLine(topCenter + glm::vec3(-radius, 0, 0), bottomCenter + glm::vec3(-radius, 0, 0), color);
        DrawLine(topCenter + glm::vec3(0, 0, radius), bottomCenter + glm::vec3(0, 0, radius), color);
        DrawLine(topCenter + glm::vec3(0, 0, -radius), bottomCenter + glm::vec3(0, 0, -radius), color);
    }

    void DebugRenderer::Upload(labut2::Allocator const& allocator) {
        if (mVertices.empty()) return;

        void* ptr;
        vmaMapMemory(allocator.allocator, mVertexBuffer.allocation, &ptr);
        std::memcpy(ptr, mVertices.data(), mVertices.size() * sizeof(DebugVertex));
        vmaUnmapMemory(allocator.allocator, mVertexBuffer.allocation);
    }

    void DebugRenderer::Render(VkCommandBuffer cmdBuff, VkPipeline debugPipeline, VkPipelineLayout layout, VkDescriptorSet sceneDesc) {
        if (mVertices.empty()) return;

        vkCmdBindPipeline(cmdBuff, VK_PIPELINE_BIND_POINT_GRAPHICS, debugPipeline);

        vkCmdBindDescriptorSets(cmdBuff, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0, 1, &sceneDesc, 0, nullptr);

        VkDeviceSize offsets[] = { 0 };
        vkCmdBindVertexBuffers(cmdBuff, 0, 1, &mVertexBuffer.buffer, offsets);

        vkCmdDraw(cmdBuff, static_cast<uint32_t>(mVertices.size()), 1, 0, 0);
    }

    void DebugRenderer::Clear() {
        mVertices.clear();
    }
}