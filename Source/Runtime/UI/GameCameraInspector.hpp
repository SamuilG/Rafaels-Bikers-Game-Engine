#pragma once

#include "../Scene/GameCameraEntity.hpp"
#include "SwitchLanguage.hpp"
#include <imgui.h>
#include <algorithm>
#include <string>

namespace engine {

inline bool IsGameCameraTakenOver(const CameraState& state)
{
    return state.mode == CameraMode::Portal || state.mode == CameraMode::Cinematic;
}

// Edit the authored configuration. Runtime orbit smoothing and temporary
// portal/cinematic poses are displayed separately and never written back here.
inline bool DrawGameCameraInspector(flecs::entity entity, const CameraState& live)
{
    auto& component = entity.get_mut<GameCameraComponent>();
    CameraSettings settings = component.settings;
    const bool takenOver = IsGameCameraTakenOver(live);
    const auto label = [](const char* text, const char* id) {
        return std::string(_SL(text)) + "###GameCamera" + id;
    };
    float labelWidth = 0.0f;
    for (const char* text : {"Camera Mode", "FOV", "Near Clip", "Far Clip", "Yaw", "Pitch", "Distance",
        "Retained Coverage", "Fade Response", "Target Height"})
        labelWidth = std::max(labelWidth, ImGui::CalcTextSize(_SL(text)).x);
    // Keep right-hand labels visible in the default narrow inspector dock.
    ImGui::PushItemWidth(std::max(48.0f,
        ImGui::GetContentRegionAvail().x - labelWidth - ImGui::GetStyle().ItemInnerSpacing.x));
    ImGui::SeparatorText(_SL("Game Camera"));
    ImGui::TextWrapped("%s", _SL("This entity controls the Game view. Settings are saved with the scene snapshot."));
    if (takenOver)
        ImGui::TextWrapped("%s", _SL("Camera settings are locked during portal and cinematic control."));
    ImGui::BeginDisabled(takenOver);

    int mode = settings.mode == CameraMode::Free ? 1 : 0;
    const char* modes[] = {_SL("Follow Player"), _SL("Free Camera")};
    bool changed = ImGui::Combo(label("Camera Mode", "Mode").c_str(), &mode, modes, 2);
    const bool enterFree = changed && mode == 1;
    if (changed) settings.mode = mode == 1 ? CameraMode::Free : CameraMode::Follow;

    ImGui::SeparatorText(_SL("Lens"));
    changed |= ImGui::Checkbox(label("Automatic FOV", "AutomaticFov").c_str(), &settings.automaticFov);
    if (ImGui::SliderFloat(label("FOV", "Fov").c_str(), &settings.fov, 10.0f, 120.0f, "%.1f deg", ImGuiSliderFlags_AlwaysClamp)) {
        settings.automaticFov = false;
        changed = true;
    }
    ImGui::TextDisabled(_SL("Current FOV: %.1f degrees"), live.cameraFov);
    ImGui::TextWrapped("%s", _SL("Automatic FOV follows player speed and orbit distance in Follow mode. Editing FOV selects a manual lens."));
    changed |= ImGui::DragFloat(label("Near Clip", "NearClip").c_str(), &settings.nearPlane, 0.01f,
        0.001f, std::max(0.001f, settings.farPlane - 0.001f), "%.3f m", ImGuiSliderFlags_AlwaysClamp);
    changed |= ImGui::DragFloat(label("Far Clip", "FarClip").c_str(), &settings.farPlane, 1.0f,
        settings.nearPlane + 0.001f, 1000000.0f, "%.1f m", ImGuiSliderFlags_AlwaysClamp);

    if (settings.mode == CameraMode::Follow) {
        ImGui::SeparatorText(_SL("Follow Orbit"));
        changed |= ImGui::SliderAngle(label("Yaw", "Yaw").c_str(), &settings.yaw, -180.0f, 180.0f, "%.1f deg", ImGuiSliderFlags_AlwaysClamp);
        changed |= ImGui::SliderAngle(label("Pitch", "Pitch").c_str(), &settings.pitch, -85.0f, 85.0f, "%.1f deg", ImGuiSliderFlags_AlwaysClamp);
        changed |= ImGui::SliderFloat(label("Distance", "Distance").c_str(), &settings.distance, 2.0f, 70.0f, "%.2f m", ImGuiSliderFlags_AlwaysClamp);
        if (ImGui::Button(label("Reset Follow Orbit", "ResetOrbit").c_str())) {
            settings.yaw = settings.pitch = 0.0f;
            settings.distance = 5.0f;
            changed = true;
        }
    }

    ImGui::SeparatorText(_SL("Camera Occlusion Fade"));
    changed |= ImGui::Checkbox(label("Enable Occlusion Fade", "OcclusionEnabled").c_str(), &settings.occlusionFadeEnabled);
    ImGui::BeginDisabled(!settings.occlusionFadeEnabled || settings.mode != CameraMode::Follow);
    changed |= ImGui::SliderFloat(label("Retained Coverage", "Coverage").c_str(), &settings.occlusionFadeCoverage,
        0.0f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
    changed |= ImGui::SliderFloat(label("Fade Response", "FadeSpeed").c_str(), &settings.occlusionFadeSpeed,
        0.1f, 30.0f, "%.1f", ImGuiSliderFlags_AlwaysClamp);
    changed |= ImGui::SliderFloat(label("Target Height", "TargetHeight").c_str(), &settings.occlusionTargetHeight,
        0.0f, 3.0f, "%.2f m", ImGuiSliderFlags_AlwaysClamp);
    ImGui::EndDisabled();
    ImGui::EndDisabled();
    ImGui::PopItemWidth();

    if (!changed || takenOver || !settings.IsValid()) return false;
    if (enterFree) component.freeTransform = live.camera2world;
    component.settings = settings;
    component.settingsDirty = true;
    entity.modified<GameCameraComponent>();
    return true;
}

} // namespace engine
