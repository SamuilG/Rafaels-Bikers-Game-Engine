#include "EngineUi.hpp"
#include "CameraGizmoGeometry.hpp"
#include "EditorTheme.hpp"
#include "EditorGizmoGeometry.hpp"
#include "../Scene/EditorSceneAdapter.hpp"
#include "../UserState/EditorState.hpp"
#include "../../ThirdParty/imgui/ImGuizmo/ImGuizmo.h"
#include <cstdio>

namespace engine {

void EngineUi::DrawPlayerCameraGizmo(VkDescriptorSet icon,
    const editor_camera_gizmo::CameraView& camera, const glm::mat4& sceneViewProjection,
    EditorState& editor, flecs::entity_t cameraEntityId, flecs::entity_t* selectedId, bool allowSelection)
{
    if (!editor.showEngineUi || !editor.isSceneViewportVisible || !editor.gizmos.visible ||
        (!editor.showPlayerCameraIcon && !editor.showPlayerCameraFrustum)) return;
    ImDrawList* drawList = GetSceneViewportDrawList();
    const ImVec2 origin = GetSceneViewportPos();
    const ImVec2 size = GetSceneViewportSize();
    if (!drawList || size.x <= 1.0f || size.y <= 1.0f) return;

    const auto displayCamera = editor_camera_gizmo::MakeFrustumDisplayView(camera,
        {editor.playerCameraFrustumPreviewDistance, editor.playerCameraFrustumUseFarClip});
    if (!displayCamera) return;
    const auto corners = editor_camera_gizmo::BuildFrustumCorners(*displayCamera);
    if (!corners) return;
    const auto screenPoint = [&](const glm::vec2& ndc) {
        return ImVec2(origin.x + (ndc.x + 1.0f) * 0.5f * size.x,
            origin.y + (1.0f - ndc.y) * 0.5f * size.y);
    };
    drawList->PushClipRect(origin, ImVec2(origin.x + size.x, origin.y + size.y), true);

    const bool selected = selectedId && cameraEntityId != 0 && *selectedId == cameraEntityId;
    if (editor.showPlayerCameraFrustum && (!editor.gizmos.selectedOnly || selected)) {
        // Close the guide at its labelled preview plane. The actual game lens
        // is untouched; full-range inspection is an explicit editor option.
        constexpr int edges[12][2] = {
            {0, 1}, {1, 2}, {2, 3}, {3, 0},
            {4, 5}, {5, 6}, {6, 7}, {7, 4},
            {0, 4}, {1, 5}, {2, 6}, {3, 7}
        };
        for (const auto& edge : edges) {
            glm::vec2 start, end;
            if (!editor_camera_gizmo::ProjectSegment(sceneViewProjection,
                (*corners)[edge[0]], (*corners)[edge[1]], start, end)) continue;
            const ImVec2 a = screenPoint(start), b = screenPoint(end);
            drawList->AddLine(a, b, editor_theme::ColorU32(editor_theme::kAccent), 1.5f);
        }
        if (!editor.playerCameraFrustumUseFarClip) {
            const glm::vec3 previewCenter = ((*corners)[4] + (*corners)[6]) * 0.5f;
            if (const auto center = editor_camera_gizmo::ProjectPoint(sceneViewProjection, previewCenter)) {
                char label[64];
                std::snprintf(label, sizeof(label), "Preview %.1f m", displayCamera->farPlane);
                const ImVec2 position = screenPoint(*center);
                const ImVec2 textPosition(position.x + 6.0f, position.y + 6.0f);
                const ImVec2 textSize = ImGui::CalcTextSize(label);
                drawList->AddRectFilled(ImVec2(textPosition.x - 3.0f, textPosition.y - 2.0f),
                    ImVec2(textPosition.x + textSize.x + 3.0f, textPosition.y + textSize.y + 2.0f),
                    editor_theme::ColorU32(editor_theme::kPaper, 0.9f), 2.0f);
                drawList->AddText(textPosition, editor_theme::ColorU32(editor_theme::kText), label);
            }
        }
    }

    const glm::vec3 position(camera.cameraToWorld[3]);
    const auto projected = editor_camera_gizmo::ProjectPoint(sceneViewProjection, position);
    if (editor.showPlayerCameraIcon && projected) {
        ImVec2 center = screenPoint(*projected);
        const float radius = editor.gizmos.iconSize * 0.5f;
        // Keep the selected marker clear of the transform handle at the pivot.
        if (selected) {
            center.x += radius + 10;
            center.y -= radius + 10;
            drawList->AddLine(screenPoint(*projected), center, editor_theme::ColorU32(editor_theme::kAccent, 0.6f));
        }
        const ImVec2 minimum(center.x - radius, center.y - radius);
        const ImVec2 maximum(center.x + radius, center.y + radius);
        drawList->AddCircleFilled(center, radius, editor_theme::ColorU32(editor_theme::kPaper, 0.92f));
        drawList->AddCircle(center, radius, editor_theme::ColorU32(selected ? editor_theme::kAccent : editor_theme::kBorder), 0, selected ? 2.0f : 1.0f);
        if (icon != VK_NULL_HANDLE) {
            // The shared texture uploader flips source rows; undo that in the UI UVs.
            drawList->AddImage((ImTextureID)icon, minimum, maximum, ImVec2(0, 1), ImVec2(1, 0));
        } else {
            // Older external content packages may not include the editor icon yet.
            const ImVec2 labelSize = ImGui::CalcTextSize("C");
            drawList->AddText(ImVec2(center.x - labelSize.x * 0.5f, center.y - labelSize.y * 0.5f),
                editor_theme::ColorU32(editor_theme::kAccent), "C");
        }
        if (editor.isSceneViewportHovered && ImGui::IsMouseHoveringRect(minimum, maximum, false)) {
            if (allowSelection && selectedId && cameraEntityId != 0 &&
                !ImGui::IsAnyItemActive() && !ImGuizmo::IsUsing() &&
                !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel) &&
                ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                *selectedId = cameraEntityId;
                editor.activeParticleIndex = -1;
                editor.showEntityInspector = true;
            }
            ImGui::BeginTooltip();
            ImGui::TextUnformatted("Player Camera");
            ImGui::TextUnformatted("Click to edit camera settings");
            ImGui::Text("Position: %.2f, %.2f, %.2f", position.x, position.y, position.z);
            ImGui::Text("FOV: %.1f deg | Aspect: %.3f", camera.verticalFovDegrees, camera.aspectRatio);
            ImGui::Text("Clip planes: %.2f - %.1f", camera.nearPlane, camera.farPlane);
            if (editor.showPlayerCameraFrustum && !editor.playerCameraFrustumUseFarClip)
                ImGui::Text("Frustum guide: %.1f m preview (display only)", displayCamera->farPlane);
            ImGui::EndTooltip();
        }
    }
    drawList->PopClipRect();
}

bool EngineUi::DrawLightGizmos(EditorSceneAdapter& scene, const glm::mat4& projection, EditorState& editor) {
    if (!editor.gizmos.visible || !scene.Scene() || (!editor.gizmos.lightIcons && !editor.gizmos.lightGuides)) return false;
    auto* draw = GetSceneViewportDrawList();
    const auto origin = GetSceneViewportPos(), size = GetSceneViewportSize();
    if (!draw || size.x <= 1 || size.y <= 1) return false;
    const auto pixel = [&](glm::vec2 ndc) {
        return ImVec2(origin.x + (ndc.x + 1) * 0.5f * size.x, origin.y + (1 - ndc.y) * 0.5f * size.y);
    };
    const ImU32 color = editor_theme::ColorU32(editor_theme::kAccent);
    bool hovered = false;
    draw->PushClipRect(origin, ImVec2(origin.x + size.x, origin.y + size.y), true);
    scene.Scene()->get_world().each([&](flecs::entity entity, const LightComponent& light, const LocalTransform& transform) {
        const auto world = scene_hierarchy::World(entity);
        const bool selected = scene.Selection() == entity.id();
        if (editor.gizmos.lightGuides && (selected || !editor.gizmos.selectedOnly)) {
            for (const auto& line : editor_gizmo::LightGuide(light, world)) {
                glm::vec2 a, b;
                if (editor_camera_gizmo::ProjectSegment(projection, line.start, line.end, a, b))
                    draw->AddLine(pixel(a), pixel(b), color, selected ? 1.5f : 1.0f);
            }
        }
        const auto point = editor_camera_gizmo::ProjectPoint(projection, glm::vec3(world[3]));
        if (!editor.gizmos.lightIcons || !point) return;
        ImVec2 center = pixel(*point);
        const float radius = editor.gizmos.iconSize * 0.5f;
        if (selected) {
            center.x += radius + 10;
            center.y -= radius + 10;
            draw->AddLine(pixel(*point), center, color);
        }
        draw->AddCircleFilled(center, radius, editor_theme::ColorU32(editor_theme::kPaper, 0.92f));
        draw->AddCircle(center, radius, editor_theme::ColorU32(selected ? editor_theme::kAccent : editor_theme::kBorder), 0, selected ? 2.0f : 1.0f);
        draw->AddCircle(center, radius * 0.32f, color, 16, 1.5f);
        for (int i = 0; i < 8; ++i) {
            const float angle = float(i) * glm::two_pi<float>() / 8;
            const float x = std::cos(angle), y = std::sin(angle);
            draw->AddLine(ImVec2(center.x + x * radius * 0.5f, center.y + y * radius * 0.5f),
                ImVec2(center.x + x * radius * 0.75f, center.y + y * radius * 0.75f), color);
        }
        const ImVec2 minimum(center.x - radius, center.y - radius), maximum(center.x + radius, center.y + radius);
        if (!editor.isSceneViewportHovered || (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_NoMouse) ||
            !ImGui::IsMouseHoveringRect(minimum, maximum) || ImGuizmo::IsUsing() ||
            ImGui::IsAnyItemActive() || ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel)) return;
        hovered = true;
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        ImGui::BeginTooltip();
        ImGui::TextUnformatted(entity.name().c_str());
        ImGui::TextUnformatted(light.type == LightType::Directional ? "Directional light" : light.type == LightType::Spot ? "Spot light" : "Point light");
        ImGui::TextUnformatted("Click to select; F to frame");
        ImGui::EndTooltip();
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            scene.Selection() = entity.id();
            editor.activeParticleIndex = -1;
            editor.showEntityInspector = true;
        }
    });
    draw->PopClipRect();
    return hovered;
}

} // namespace engine
