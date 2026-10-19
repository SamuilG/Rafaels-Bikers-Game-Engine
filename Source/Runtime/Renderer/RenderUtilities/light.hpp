#pragma once
#include <glm/glm.hpp>
#include <cstdint>
#include <cmath>
#include <algorithm>

namespace engine {
inline constexpr uint32_t count = 4;
inline constexpr uint32_t MAX_LIGHTS = 16;
enum class LightType : uint32_t { Directional = 0, Point = 1, Spot = 2 };

struct DirectionalShadowSettings {
    float firstSplit = 15.0f, secondSplit = 70.0f, distance = 800.0f;
    float receiverBias = 0.00005f, filterRadius = 2.5f, blendFraction = 0.12f, strength = 1.0f;
    float constantBias = 1.5f, slopeBias = 1.85f, casterPadding = 3000.0f, depthPadding = 500.0f;
    bool IsValid() const {
        const float values[] = {firstSplit, secondSplit, distance, receiverBias, filterRadius,
            blendFraction, strength, constantBias, slopeBias, casterPadding, depthPadding};
        for (float value : values) if (!std::isfinite(value)) return false;
        return firstSplit >= 0.01f && firstSplit < secondSplit && secondSplit < distance && distance <= 10000.0f &&
            receiverBias >= 0 && receiverBias <= 0.02f && filterRadius >= 0 && filterRadius <= 10 &&
            blendFraction >= 0 && blendFraction <= 0.5f && strength >= 0 && strength <= 1 &&
            constantBias >= 0 && constantBias <= 20 && slopeBias >= 0 && slopeBias <= 20 &&
            casterPadding >= 0 && casterPadding <= 10000 && depthPadding >= 0.01f && depthPadding <= 10000;
    }
};

struct LightComponent {
    glm::vec3 color{1.0f};
    float intensity = 1.0f;
    LightType type = LightType::Point;
    float range = 10.0f, specularMultiplier = 1.0f;
    // Local direction in which light rays travel; translation never defines it.
    glm::vec3 direction{0, 0, -1};
    float innerCutOff = 12.5f, outerCutOff = 17.5f;
    bool primaryDirectional = false, castsShadows = true;
    DirectionalShadowSettings shadow;
    bool IsValid() const {
        if (type != LightType::Directional && type != LightType::Point && type != LightType::Spot) return false;
        for (int axis = 0; axis < 3; ++axis)
            if (!std::isfinite(color[axis]) || color[axis] < 0 || color[axis] > 100000 ||
                !std::isfinite(direction[axis]) || std::abs(direction[axis]) > 100000) return false;
        return glm::dot(direction, direction) > 1e-12f && std::isfinite(intensity) && intensity >= 0 && intensity <= 100000000 &&
            std::isfinite(specularMultiplier) && specularMultiplier >= 0 && specularMultiplier <= 100000000 &&
            std::isfinite(range) && range >= (type == LightType::Directional ? 0.0f : 0.001f) && range <= 10000000 &&
            std::isfinite(innerCutOff) && std::isfinite(outerCutOff) && innerCutOff >= 0 && outerCutOff <= 89.9f &&
            innerCutOff <= outerCutOff && (type != LightType::Spot || outerCutOff >= 0.01f) && shadow.IsValid();
    }
};

inline glm::vec3 NormalizeLightDirection(const glm::vec3& direction) {
    const float length2 = glm::dot(direction, direction);
    return std::isfinite(length2) && length2 > 1e-12f ? direction / std::sqrt(length2) : glm::vec3(0, -1, 0);
}
struct alignas(16) GpuLight {
    glm::vec4 position{};  // position, or direction toward source; w: type
    glm::vec4 color{};     // RGB and intensity
    glm::vec4 direction{}; // world ray direction and range
    glm::vec4 params{};    // cos(inner), cos(outer), specular, assigned shadow (0/1/2)
};
static_assert(sizeof(GpuLight) == 64);
inline GpuLight MakeGpuLight(const LightComponent& light, const glm::mat4& world) {
    GpuLight result;
    const auto rays = NormalizeLightDirection(glm::vec3(world * glm::vec4(light.direction, 0)));
    result.position = glm::vec4(light.type == LightType::Directional ? -rays : glm::vec3(world[3]), float(light.type));
    result.color = glm::vec4(light.color, light.intensity);
    result.direction = glm::vec4(rays, std::max(light.range, 0.001f));
    result.params = {std::cos(glm::radians(light.innerCutOff)), std::cos(glm::radians(light.outerCutOff)), light.specularMultiplier, 0};
    return result;
}
struct DirectionalLightState {
    uint64_t entityId = 0;
    GpuLight light;
    DirectionalShadowSettings shadow;
    bool castsShadows = false;
};
// Appended to SceneUniform without moving the existing shader/IBL prefix.
struct DirectionalShadowGpu {
    glm::vec4 sampling{0.00005f, 2.5f, 0.12f, 1.0f}; // receiver bias, filter, blend, strength
    glm::vec4 raster{1.5f, 1.85f, 1.0f, 0.0f};       // constant, slope, enabled, unused
};
inline DirectionalShadowGpu MakeDirectionalShadowGpu(const DirectionalLightState& light) {
    const auto& s = light.shadow;
    return {{s.receiverBias, s.filterRadius, s.blendFraction, s.strength},
        {s.constantBias, s.slopeBias, light.castsShadows ? 1.0f : 0.0f, 0.0f}};
}
struct ShadowData {
    glm::vec4 cascadeSplits{};
    glm::mat4 lightVP[count]{glm::mat4(1), glm::mat4(1), glm::mat4(1), glm::mat4(1)};
};
ShadowData compute_csm_matrices(const glm::vec3& directionTowardLight, const glm::mat4& cameraView,
    float fov, float aspect, float nearP, float farP, const DirectionalShadowSettings& settings = {});
glm::mat4 compute_spotlight_matrix(const glm::vec3& pos, const glm::vec3& dir, float outerCutOff, float range);

struct DirectionalShadowFrame {
    GpuLight source;
    DirectionalShadowGpu parameters;
    ShadowData cascades;
};
inline DirectionalShadowFrame BuildDirectionalShadowFrame(const DirectionalLightState& light,
    const glm::mat4& cameraView, float fov, float aspect, float nearPlane, float farPlane) {
    DirectionalShadowFrame frame{light.light, MakeDirectionalShadowGpu(light), {}};
    if (light.castsShadows)
        frame.cascades = compute_csm_matrices(glm::vec3(frame.source.position), cameraView,
            fov, aspect, nearPlane, farPlane, light.shadow);
    if (frame.cascades.cascadeSplits.z <= 0) frame.parameters.raster.z = 0;
    return frame;
}
} // namespace engine
