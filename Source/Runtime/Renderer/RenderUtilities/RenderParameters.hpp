#pragma once
#include "../../UserState/RenderSettings.hpp"
#include <cstddef>
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
struct CameraPostParameters {
    float speedFactor = 0.0f;
    float deathFactor = 0.0f;
    float portalStrength = 0.0f;
    float portalTime = 0.0f;
    float srgbOutput = 1.0f;
    alignas(16) glm::mat4 waveWorldFromClip{1.0f};
    glm::vec4 waveOriginRadius{0.0f};
    glm::vec4 waveShape{0.0f};
};
struct PortalGroundParameters {
    glm::mat4 viewProjection;
    glm::vec4 originRadius;
    glm::vec4 shape;
    glm::vec4 grid;
    glm::vec4 baseColorFactor;
};
struct PortalWaveSurfaceParameters {
    glm::mat4 worldFromClip;
    glm::vec4 originRadius;
    glm::vec4 shape;
    glm::vec4 clipPlane;
};
struct PortalGroundMaterialMapping {
    glm::vec4 uAndSlab;
    glm::vec4 v;
};
static_assert(sizeof(EnvironmentParameters) == 32);
static_assert(sizeof(CompositeParameters) == 48);
static_assert(offsetof(CameraPostParameters, waveWorldFromClip) == 32);
static_assert(sizeof(CameraPostParameters) == 128);
static_assert(sizeof(PortalGroundParameters) == 128);
static_assert(sizeof(PortalWaveSurfaceParameters) == 112);
static_assert(sizeof(PortalGroundMaterialMapping) == 32);
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
