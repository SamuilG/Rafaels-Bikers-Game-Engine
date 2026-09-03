#include <cstdlib>
#include <iostream>

#include <glm/gtc/matrix_transform.hpp>

#include "Runtime/Renderer/RenderSnapshotGpuData.hpp"
#include "Runtime/Renderer/RenderBatchOrdering.hpp"
#include "Runtime/Renderer/StaticInstanceBatching.hpp"
#include "Runtime/Scene/SceneRenderExtraction.hpp"
#include "Runtime/Scene/SceneTransformProgression.hpp"
#include "Runtime/Scene/SceneCameraView.hpp"
#include "Runtime/Scene/SceneFrustumCulling.hpp"

namespace
{
    void Check(bool condition, const char* message)
    {
        if (!condition) {
            std::cerr << "FAILED: " << message << '\n';
            std::exit(1);
        }
    }

    bool SameMatrix(const glm::mat4& left, const glm::mat4& right)
    {
        for (int column = 0; column < 4; ++column) {
            for (int row = 0; row < 4; ++row) {
                if (left[column][row] != right[column][row]) return false;
            }
        }
        return true;
    }

    void SnapshotDataIsCopiedIntoGpuLayout()
    {
        RenderInstance instance{};
        instance.renderableAssetId = 42;
        instance.worldTransform = glm::translate(glm::mat4(1.0f), { 3.0f, -2.0f, 7.0f });
        instance.opacity = 0.35f;

        const engine::rendering::GpuInstanceData gpu =
            engine::rendering::MakeGpuInstanceData(instance);

        Check(SameMatrix(gpu.worldTransform, instance.worldTransform),
              "GPU instance transform must equal the Snapshot transform");
        Check(gpu.opacityAndPadding.x == 0.35f,
              "GPU instance opacity must equal the Snapshot opacity");
        Check(gpu.opacityAndPadding.y == 0.0f && gpu.opacityAndPadding.z == 0.0f &&
                  gpu.opacityAndPadding.w == 0.0f,
              "GPU instance padding must be deterministic");
    }

    void SnapshotIndicesAndCompatibilityBatchesStaySeparate()
    {
        Check(engine::rendering::UsesSnapshotInstance(9),
              "Snapshot batches must use their own instance index");
        Check(engine::rendering::DrawFirstInstance(9) == 9,
              "Snapshot batch draw must pass its index as firstInstance");
        Check(!engine::rendering::UsesSnapshotInstance(engine::rendering::kLegacyInstanceIndex),
              "Preview and portal compatibility batches must not use Snapshot data");
        Check(engine::rendering::DrawFirstInstance(engine::rendering::kLegacyInstanceIndex) == 0,
              "Compatibility batches must use a neutral firstInstance");
    }

    void FrameResourcesAlternateWithoutOverlap()
    {
        Check(engine::rendering::FrameSlot(0, 2) == 0, "first frame must use slot zero");
        Check(engine::rendering::FrameSlot(1, 2) == 1, "second frame must use slot one");
        Check(engine::rendering::FrameSlot(2, 2) == 0, "third frame must return to slot zero");
        Check(engine::rendering::FrameSlot(7, 2) == 1, "frame-slot selection must remain stable");
    }

    void SceneExtractionAppliesVisibilityRules()
	{
		const glm::mat4 transform = glm::translate(glm::mat4(1.0f), { 2.0f, 0.0f, -5.0f });
		const auto visible = engine::scene_render_extraction::MakeRenderInstance(7, transform, 1.5f, true);
		Check(visible.has_value() && visible->opacity == 1.0f,
			"scene extraction must clamp opacity above one");
		Check(!engine::scene_render_extraction::MakeRenderInstance(7, transform, 0.001f, true).has_value(),
			"scene extraction must omit fully invisible instances");
		Check(!engine::scene_render_extraction::MakeRenderInstance(kInvalidRenderableAssetId, transform, 1.0f, true).has_value(),
			"scene extraction must omit missing renderable assets");
    }

	void TransformProgressionComposesHierarchy()
	{
		const glm::mat4 root = glm::translate(glm::mat4(1.0f), { 10.0f, 0.0f, 0.0f });
		const glm::mat4 child = glm::translate(glm::mat4(1.0f), { 0.0f, 3.0f, 0.0f });
		const glm::mat4 grandchild = glm::translate(glm::mat4(1.0f), { 0.0f, 0.0f, -2.0f });
		const glm::mat4 childWorld = engine::scene_transform::ComposeWorldTransform(&root, child);
		const glm::mat4 grandchildWorld = engine::scene_transform::ComposeWorldTransform(&childWorld, grandchild);
		Check(glm::vec3(childWorld[3]) == glm::vec3(10.0f, 3.0f, 0.0f), "child world transform must inherit root movement");
		Check(glm::vec3(grandchildWorld[3]) == glm::vec3(10.0f, 3.0f, -2.0f), "grandchild world transform must inherit full hierarchy");
		Check(SameMatrix(engine::scene_transform::ComposeWorldTransform(nullptr, root), root), "root world transform must equal its local transform");
	}

    void CullingUsesWorldSpaceAabbs()
    {
        glm::vec3 worldMin{};
        glm::vec3 worldMax{};
        const glm::mat4 transform = glm::translate(glm::mat4(1.0f), { 8.0f, 0.0f, 0.0f }) *
            glm::scale(glm::mat4(1.0f), { 2.0f, 3.0f, 4.0f });
        engine::TransformAabb({ -1.0f, -1.0f, -1.0f }, { 1.0f, 1.0f, 1.0f }, transform, worldMin, worldMax);
        Check(worldMin == glm::vec3(6.0f, -3.0f, -4.0f), "world AABB minimum must include entity transform");
        Check(worldMax == glm::vec3(10.0f, 3.0f, 4.0f), "world AABB maximum must include entity transform");

        engine::Frustum frustum{};
        frustum.planes[0] = { { 1.0f, 0.0f, 0.0f }, 5.0f };
        Check(engine::IntersectsAabb(frustum, worldMin, worldMax), "AABB on the visible side of a frustum plane must survive");
        frustum.planes[0] = { { 1.0f, 0.0f, 0.0f }, -11.0f };
        Check(!engine::IntersectsAabb(frustum, worldMin, worldMax), "AABB fully outside a frustum plane must be culled");
        Check(engine::IntersectsAabb(frustum, worldMin, worldMax, 2.0f), "frustum padding must retain near-edge AABBs");
    }

    void RenderOrderingKeepsTransparencyCorrect()
    {
        const auto at = [](RenderableAssetId assetId, float z, float opacity, bool materialAlphaBlend) {
            engine::rendering::RenderOrderingInput input{};
            input.renderableAssetId = assetId;
            input.worldTransform = glm::translate(glm::mat4(1.0f), { 0.0f, 0.0f, z });
            input.opacity = opacity;
            input.materialAlphaBlend = materialAlphaBlend;
            return input;
        };
        const glm::vec3 cameraPosition(0.0f);
        const auto opaque = at(9, 1.0f, 1.0f, false);
        const auto alphaByMaterial = at(3, 2.0f, 1.0f, true);
        const auto alphaByEntity = at(4, 10.0f, 0.5f, false);

        Check(!engine::rendering::IsTransparent(opaque), "opaque material at full opacity must stay opaque");
        Check(engine::rendering::IsTransparent(alphaByMaterial), "alpha-blend material must be transparent");
        Check(engine::rendering::IsTransparent(alphaByEntity), "entity opacity must make an instance transparent");
        Check(engine::rendering::DrawsBefore(opaque, alphaByMaterial, cameraPosition),
              "opaque instances must draw before transparent instances");
        Check(engine::rendering::DrawsBefore(alphaByEntity, alphaByMaterial, cameraPosition),
              "far transparent instances must draw before near transparent instances");
        Check(engine::rendering::DrawsBefore(at(2, 1.0f, 1.0f, false), at(8, 1.0f, 1.0f, false), cameraPosition),
              "opaque instances must have a stable asset-based order");
    }

    void StaticInstancesWithTheSameAssetShareOneDraw()
    {
        engine::rendering::StaticInstanceCandidate first{};
        first.meshIndex = 2;
        first.materialIndex = 5;
        first.renderableAssetId = 77;
        first.instanceIndex = 10;

        engine::rendering::StaticInstanceCandidate second = first;
        second.instanceIndex = 11;

        const auto groups = engine::rendering::BuildStaticInstanceDrawGroups({ first, second });
        Check(groups.size() == 1, "two opaque Snapshot instances of one asset must form one draw group");
        Check(groups[0].meshIndex == 2 && groups[0].materialIndex == 5,
              "draw group must retain the asset mesh and material");
        Check(groups[0].firstInstance == 10 && groups[0].instanceCount == 2,
              "draw group must cover the consecutive Snapshot instance range");

        const auto splitGroups = engine::rendering::BuildStaticInstanceDrawGroups({
            first,
            { 2, 5, 78, 11, false },
            { 2, 5, 77, 12, true },
            { 2, 5, 77, engine::rendering::kLegacyInstanceIndex, false }
        });
        Check(splitGroups.size() == 2,
              "different assets, transparent instances, and legacy batches must not join an instanced group");
        Check(splitGroups[0].instanceCount == 1 && splitGroups[1].instanceCount == 1,
              "only eligible consecutive Snapshot instances may be merged");
    }
}

int main()
{
    SnapshotDataIsCopiedIntoGpuLayout();
    SnapshotIndicesAndCompatibilityBatchesStaySeparate();
    FrameResourcesAlternateWithoutOverlap();
	SceneExtractionAppliesVisibilityRules();
	TransformProgressionComposesHierarchy();
	CullingUsesWorldSpaceAabbs();
	RenderOrderingKeepsTransparencyCorrect();
	StaticInstancesWithTheSameAssetShareOneDraw();
	const glm::mat4 cameraWorld = glm::translate(glm::mat4(1.0f), { 1.0f, 2.0f, 3.0f });
	Check(engine::scene_camera::MakeCameraView(nullptr, 60.0f, .1f, 1000.0f, 1.0f) == std::nullopt, "missing camera must not produce CameraView");
	const auto camera = engine::scene_camera::MakeCameraView(&cameraWorld, 60.0f, .1f, 1000.0f, 16.0f / 9.0f);
	Check(camera.has_value() && camera->worldPosition == glm::vec3(1.0f, 2.0f, 3.0f), "CameraView must use final world transform");
    std::cout << "RenderSnapshot regression tests passed\n";
    return 0;
}
