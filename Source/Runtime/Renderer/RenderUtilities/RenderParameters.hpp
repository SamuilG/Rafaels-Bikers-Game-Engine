#pragma once
#include "../../UserState/RenderSettings.hpp"
#include <glm/glm.hpp>

namespace glsl {
struct EnvironmentParameters {
    glm::vec4 scalesRotation{1.0f, 1.0f, 1.0f, 0.0f};
    glm::vec4 tintSaturation{1.0f};
};
struct CompositeParameters {
    float exposure = 1.0f;
    float bloomStrength = 1.2f;
    float editorBackdrop = 0.0f;
    float srgbOutput = 1.0f;
    glm::vec4 acesCurve{2.51f, 0.03f, 2.43f, 0.59f};
    glm::vec4 acesOptions{0.14f, 1.0f, 0.0f, 0.0f};
};
static_assert(sizeof(EnvironmentParameters) == 32);
static_assert(sizeof(CompositeParameters) == 48);
}

namespace engine {
inline glsl::EnvironmentParameters MakeEnvironmentParameters(const EnvironmentLightingSettings& settings) {
    return {
        {settings.intensity, settings.diffuseIntensity, settings.specularIntensity, glm::radians(settings.rotationDegrees)},
        {settings.tint[0], settings.tint[1], settings.tint[2], settings.saturation}
    };
}
inline glsl::CompositeParameters MakeCompositeParameters(float exposure, float bloomStrength,
    bool editorBackdrop, bool srgbOutput, const ToneMappingSettings& settings) {
    return {exposure, bloomStrength, editorBackdrop ? 1.0f : 0.0f, srgbOutput ? 1.0f : 0.0f,
        {settings.acesCurve[0], settings.acesCurve[1], settings.acesCurve[2], settings.acesCurve[3]},
        {settings.acesCurve[4], settings.acesEnabled ? 1.0f : 0.0f, 0.0f, 0.0f}};
}
}
