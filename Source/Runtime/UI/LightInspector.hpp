#pragma once
#include "../Renderer/RenderUtilities/light.hpp"
#include "SwitchLanguage.hpp"
#include <imgui.h>
#include <string>

namespace engine {
inline bool DrawLightInspector(LightComponent& authored, bool activePrimary, bool& makePrimary) {
    LightComponent light = authored;
    const auto label = [](const char* text, const char* id) { return std::string(_SL(text)) + "###Light" + id; };
    float labelWidth = 0;
    for (const char* text : {"Type", "Intensity", "Specular", "Range", "Ray Direction", "Inner Cone", "Outer Cone",
        "Shadow Distance", "First Split", "Second Split", "Shadow Strength", "Receiver Bias", "Filter Radius",
        "Cascade Blend", "Constant Bias", "Slope Bias", "Caster Padding", "Depth Padding"})
        labelWidth = std::max(labelWidth, ImGui::CalcTextSize(_SL(text)).x);
    ImGui::PushItemWidth(std::max(48.0f, ImGui::GetContentRegionAvail().x - labelWidth - ImGui::GetStyle().ItemInnerSpacing.x));
    ImGui::SeparatorText(_SL("Light"));
    const char* types[] = {_SL("Directional"), _SL("Point"), _SL("Spot")};
    int type = int(light.type);
    bool changed = ImGui::Combo(label("Type", "Type").c_str(), &type, types, 3);
    if (changed) {
        light.type = LightType(type);
        if (light.type != LightType::Directional) light.range = std::max(light.range, 0.001f);
        if (light.type == LightType::Spot) light.outerCutOff = std::max(light.outerCutOff, 0.01f);
        if (light.type != LightType::Directional) light.primaryDirectional = false;
    }
    changed |= ImGui::ColorEdit3(label("Color", "Color").c_str(), &light.color.x, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR);
    changed |= ImGui::DragFloat(label("Intensity", "Intensity").c_str(), &light.intensity, 0.1f, 0, 100000, "%.2f", ImGuiSliderFlags_AlwaysClamp);
    changed |= ImGui::SliderFloat(label("Specular", "Specular").c_str(), &light.specularMultiplier, 0, 4, "%.2f", ImGuiSliderFlags_AlwaysClamp);
    if (light.type != LightType::Directional)
        changed |= ImGui::DragFloat(label("Range", "Range").c_str(), &light.range, 0.1f, 0.001f, 100000, "%.2f m", ImGuiSliderFlags_AlwaysClamp);
    if (light.type != LightType::Point) {
        ImGui::TextWrapped("%s", _SL("Ray direction is local to the entity. Rotation affects lighting and shadows together."));
        if (ImGui::DragFloat3(label("Ray Direction", "Direction").c_str(), &light.direction.x, 0.01f, -1, 1, "%.3f", ImGuiSliderFlags_AlwaysClamp)) {
            light.direction = NormalizeLightDirection(light.direction);
            changed = true;
        }
    }
    if (light.type == LightType::Spot) {
        changed |= ImGui::SliderFloat(label("Inner Cone", "InnerCone").c_str(), &light.innerCutOff, 0, light.outerCutOff, "%.1f deg", ImGuiSliderFlags_AlwaysClamp);
        changed |= ImGui::SliderFloat(label("Outer Cone", "OuterCone").c_str(), &light.outerCutOff,
            std::max(light.innerCutOff, 0.01f), 89, "%.1f deg", ImGuiSliderFlags_AlwaysClamp);
        changed |= ImGui::Checkbox(label("Cast Shadows", "Shadows").c_str(), &light.castsShadows);
    }
    if (light.type == LightType::Directional) {
        if (ImGui::RadioButton(label("Primary Directional Light", "Primary").c_str(), activePrimary) && !activePrimary) {
            light.primaryDirectional = true;
            makePrimary = changed = true;
        }
        ImGui::TextWrapped("%s", _SL("One primary directional light owns the cascade shadows. Other directional lights add illumination."));
        changed |= ImGui::Checkbox(label("Cast Shadows", "Shadows").c_str(), &light.castsShadows);
        ImGui::BeginDisabled(!light.castsShadows);
        auto& s = light.shadow;
        ImGui::SeparatorText(_SL("Directional Shadows"));
        changed |= ImGui::DragFloat(label("Shadow Distance", "Distance").c_str(), &s.distance, 1,
            s.secondSplit + 0.01f, 10000, "%.1f m", ImGuiSliderFlags_AlwaysClamp);
        changed |= ImGui::DragFloat(label("First Split", "FirstSplit").c_str(), &s.firstSplit, 0.1f,
            0.01f, s.secondSplit - 0.01f, "%.1f m", ImGuiSliderFlags_AlwaysClamp);
        changed |= ImGui::DragFloat(label("Second Split", "SecondSplit").c_str(), &s.secondSplit, 0.1f,
            s.firstSplit + 0.01f, s.distance - 0.01f, "%.1f m", ImGuiSliderFlags_AlwaysClamp);
        changed |= ImGui::SliderFloat(label("Shadow Strength", "Strength").c_str(), &s.strength, 0, 1, "%.2f", ImGuiSliderFlags_AlwaysClamp);
        changed |= ImGui::DragFloat(label("Receiver Bias", "ReceiverBias").c_str(), &s.receiverBias, 0.00001f, 0, 0.02f, "%.5f", ImGuiSliderFlags_AlwaysClamp);
        changed |= ImGui::SliderFloat(label("Filter Radius", "FilterRadius").c_str(), &s.filterRadius, 0, 10, "%.2f texels", ImGuiSliderFlags_AlwaysClamp);
        changed |= ImGui::SliderFloat(label("Cascade Blend", "Blend").c_str(), &s.blendFraction, 0, 0.5f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
        if (ImGui::TreeNode(label("Shadow Depth Settings", "DepthSettings").c_str())) {
            changed |= ImGui::DragFloat(label("Constant Bias", "ConstantBias").c_str(), &s.constantBias, 0.01f, 0, 20, "%.2f", ImGuiSliderFlags_AlwaysClamp);
            changed |= ImGui::DragFloat(label("Slope Bias", "SlopeBias").c_str(), &s.slopeBias, 0.01f, 0, 20, "%.2f", ImGuiSliderFlags_AlwaysClamp);
            changed |= ImGui::DragFloat(label("Caster Padding", "CasterPadding").c_str(), &s.casterPadding, 1, 0, 10000, "%.1f m", ImGuiSliderFlags_AlwaysClamp);
            changed |= ImGui::DragFloat(label("Depth Padding", "DepthPadding").c_str(), &s.depthPadding, 1, 0.01f, 10000, "%.1f m", ImGuiSliderFlags_AlwaysClamp);
            ImGui::TreePop();
        }
        ImGui::TextDisabled("4096 x 4096 | 3 cascades");
        ImGui::EndDisabled();
    }
    ImGui::PopItemWidth();
    if (!changed || !light.IsValid()) return false;
    authored = light;
    return true;
}
} // namespace engine
