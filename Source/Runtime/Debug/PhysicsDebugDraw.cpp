#include "PhysicsDebugDraw.hpp"

#include <vector>

namespace engine::physics_debug {

    namespace {

        glm::vec3 ToGlm(const JPH::Vec3& value)
        {
            return glm::vec3(value.GetX(), value.GetY(), value.GetZ());
        }

        void DrawTransformedShapeTriangles(
            DebugRenderer& debugRenderer,
            const JPH::TransformedShape& transformedShape,
            const glm::vec3& color
        )
        {
            JPH::TransformedShape::GetTrianglesContext context;
            constexpr int kTriangleBatchSize = JPH::Shape::cGetTrianglesMinTrianglesRequested;
            JPH::Float3 triangleVertices[kTriangleBatchSize * 3];

            transformedShape.GetTrianglesStart(context, transformedShape.GetWorldSpaceBounds(), JPH::RVec3::sZero());

            while (true) {
                int triangleCount = transformedShape.GetTrianglesNext(context, kTriangleBatchSize, triangleVertices);
                if (triangleCount <= 0) {
                    break;
                }

                for (int triangleIndex = 0; triangleIndex < triangleCount; ++triangleIndex) {
                    const int baseIndex = triangleIndex * 3;
                    glm::vec3 p0 = ToGlm(JPH::Vec3::sLoadFloat3Unsafe(triangleVertices[baseIndex + 0]));
                    glm::vec3 p1 = ToGlm(JPH::Vec3::sLoadFloat3Unsafe(triangleVertices[baseIndex + 1]));
                    glm::vec3 p2 = ToGlm(JPH::Vec3::sLoadFloat3Unsafe(triangleVertices[baseIndex + 2]));

                    debugRenderer.DrawTriangleWireframe(p0, p1, p2, color);
                }
            }
        }

    } // namespace

    void DrawSelectionBounds(
        DebugRenderer& debugRenderer,
        const JPH::TransformedShape& transformedShape,
        const glm::vec3& color
    )
    {
        JPH::AABox aabb = transformedShape.GetWorldSpaceBounds();
        glm::vec3 center = ToGlm(aabb.GetCenter());
        glm::vec3 extents = ToGlm(aabb.GetExtent());
        debugRenderer.DrawBox(center, extents, color);
    }

    void DrawCollisionShapeWireframe(
        DebugRenderer& debugRenderer,
        const JPH::TransformedShape& transformedShape,
        const glm::vec3& color
    )
    {
        struct LeafCollector final : public JPH::TransformedShapeCollector
        {
            std::vector<JPH::TransformedShape> hits;

            void AddHit(const JPH::TransformedShape& inResult) override
            {
                hits.push_back(inResult);
            }
        };

        LeafCollector collector;
        transformedShape.CollectTransformedShapes(transformedShape.GetWorldSpaceBounds(), collector);

        if (collector.hits.empty()) {
            DrawTransformedShapeTriangles(debugRenderer, transformedShape, color);
            return;
        }

        for (const JPH::TransformedShape& leafShape : collector.hits) {
            DrawTransformedShapeTriangles(debugRenderer, leafShape, color);
        }
    }

} // namespace engine::physics_debug
