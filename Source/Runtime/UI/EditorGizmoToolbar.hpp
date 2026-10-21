#pragma once

#include "../UserState/EditorGizmoSettings.hpp"
#include "EditorTheme.hpp"
#include <imgui.h>
#include <ImGuizmo/ImGuizmo.h>
#include <array>
#include <cmath>

namespace engine::editor_gizmo {
inline ImGuizmo::OPERATION Operation(EditorTransformTool tool) {
    switch (tool) {
    case EditorTransformTool::Rotate: return ImGuizmo::ROTATE;
    case EditorTransformTool::Scale: return ImGuizmo::SCALE;
    default: return ImGuizmo::TRANSLATE;
    }
}
inline ImGuizmo::MODE Space(const EditorGizmoSettings& settings, EditorTransformTool tool) {
    // ImGuizmo's scale handles are local by definition; expose this in the UI.
    return settings.localSpace || tool == EditorTransformTool::Scale ? ImGuizmo::LOCAL : ImGuizmo::WORLD;
}
inline std::array<float, 3> SnapValues(const EditorGizmoSettings& settings, EditorTransformTool tool) {
    const float value = tool == EditorTransformTool::Rotate ? settings.rotateSnap :
        tool == EditorTransformTool::Scale ? settings.scaleSnap : settings.moveSnap;
    const float safe = std::isfinite(value) && value > 0 ? value : 1.0f;
    return {safe, safe, safe};
}
inline void DrawVisibilityOptions(EditorGizmoSettings& settings, bool& cameraIcon,
    bool& cameraFrustum, bool& fullFrustum, float& previewDistance) {
    ImGui::Checkbox("Show Gizmos", &settings.visible);
    ImGui::BeginDisabled(!settings.visible);
    ImGui::Checkbox("Guides for selection only", &settings.selectedOnly);
    ImGui::Checkbox("Camera icons", &cameraIcon);
    ImGui::Checkbox("Camera frustum", &cameraFrustum);
    ImGui::Checkbox("Light icons", &settings.lightIcons);
    ImGui::Checkbox("Light direction / range", &settings.lightGuides);
    ImGui::Checkbox("Particle icons", &settings.particleIcons);
    ImGui::SliderFloat("Icon size", &settings.iconSize, 16, 48, "%.0f px", ImGuiSliderFlags_AlwaysClamp);
    ImGui::BeginDisabled(!cameraFrustum);
    ImGui::Checkbox("Use camera far clip", &fullFrustum);
    ImGui::BeginDisabled(fullFrustum);
    ImGui::SliderFloat("Frustum preview", &previewDistance, 0.5f, 30.0f, "%.1f m", ImGuiSliderFlags_AlwaysClamp);
    ImGui::EndDisabled();
    ImGui::TextDisabled("Guide only; game lens is unchanged.");
    ImGui::EndDisabled();
    ImGui::EndDisabled();
    ImGui::Separator();
    ImGui::SliderFloat("Handle size", &settings.handleSize, 0.07f, 0.25f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
}

struct ToolbarResult { bool hovered = false; bool focus = false; };
// A dedicated strip reserves its own pixels; no controls float over pickable geometry.
inline ToolbarResult DrawToolbar(EditorGizmoSettings& settings, bool canFocus,
    bool canTransform, bool moveOnly, bool noScale, bool& cameraIcon, bool& cameraFrustum,
    bool& fullFrustum, float& previewDistance) {
    ToolbarResult result;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(4, 3));
    ImGui::BeginChild("SceneTools", ImVec2(0, ImGui::GetFrameHeight() * 2 + 12),
        ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4, 2));
    const auto toolButton = [&](const char* label, EditorTransformTool tool, bool enabled) {
        const bool active = settings.tool == tool;
        ImGui::BeginDisabled(!enabled);
        if (active) ImGui::PushStyleColor(ImGuiCol_Button, editor_theme::Color(editor_theme::kAccent, 0.5f));
        if (ImGui::Button(label)) settings.tool = tool;
        if (active) ImGui::PopStyleColor();
        ImGui::EndDisabled();
        ImGui::SameLine();
    };
    toolButton("Q Select", EditorTransformTool::Select, true);
    toolButton("W Move", EditorTransformTool::Move, canTransform);
    toolButton("E Rotate", EditorTransformTool::Rotate, canTransform && !moveOnly);
    toolButton("R Scale", EditorTransformTool::Scale, canTransform && !moveOnly && !noScale);
    ImGui::BeginDisabled(!canFocus);
    result.focus = ImGui::Button("F Frame");
    ImGui::EndDisabled();
    ImGui::BeginDisabled(settings.tool == EditorTransformTool::Scale);
    if (ImGui::Button(settings.tool == EditorTransformTool::Scale ? "Local (scale)" : settings.localSpace ? "Local" : "World"))
        settings.localSpace = !settings.localSpace;
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::Checkbox("Snap", &settings.snap);
    ImGui::SameLine();
    if (ImGui::Button("Steps")) ImGui::OpenPopup("SnapSteps");
    if (ImGui::BeginPopup("SnapSteps")) {
        ImGui::TextUnformatted("Hold Ctrl while dragging to snap.");
        ImGui::DragFloat("Move (m)", &settings.moveSnap, 0.1f, 0.001f, 1000, "%.3f", ImGuiSliderFlags_AlwaysClamp);
        ImGui::DragFloat("Rotate (deg)", &settings.rotateSnap, 1, 0.1f, 180, "%.1f", ImGuiSliderFlags_AlwaysClamp);
        ImGui::DragFloat("Scale", &settings.scaleSnap, 0.01f, 0.001f, 10, "%.3f", ImGuiSliderFlags_AlwaysClamp);
        ImGui::EndPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button(settings.visible ? "Gizmos" : "Gizmos (off)")) ImGui::OpenPopup("GizmoDisplay");
    if (ImGui::BeginPopup("GizmoDisplay")) {
        DrawVisibilityOptions(settings, cameraIcon, cameraFrustum, fullFrustum, previewDistance);
        ImGui::EndPopup();
    }
    result.hovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) || ImGui::IsAnyItemActive() ||
        ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
    ImGui::PopStyleVar();
    ImGui::EndChild();
    ImGui::PopStyleVar();
    return result;
}
}
