#pragma once

#include "CameraGizmoGeometry.hpp"
#include "../Renderer/RenderUtilities/light.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/constants.hpp>
#include <vector>
#include <optional>

namespace engine::editor_gizmo {
struct Segment { glm::vec3 start, end; };

// Use the same transformed ray direction and range as the light uploader.
inline std::vector<Segment> LightGuide(const LightComponent& light, const glm::mat4& transform) {
    std::vector<Segment> lines;
    if (!light.IsValid() || !editor_camera_gizmo::detail::Finite(transform)) return lines;
    const auto gpu = MakeGpuLight(light, transform);
    const glm::vec3 origin(transform[3]);
    const glm::vec3 direction = NormalizeLightDirection(glm::vec3(gpu.direction));
    const glm::vec3 right = glm::normalize(glm::cross(direction,
        std::abs(direction.y) < 0.9f ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0)));
    const glm::vec3 up = glm::cross(right, direction);
    const auto circle = [&](glm::vec3 center, glm::vec3 x, glm::vec3 y, float radius) {
        constexpr int steps = 48;
        for (int i = 0; i < steps; ++i) {
            const float a = glm::two_pi<float>() * float(i) / steps;
            const float b = glm::two_pi<float>() * float(i + 1) / steps;
            lines.push_back({center + radius * (x * std::cos(a) + y * std::sin(a)),
                center + radius * (x * std::cos(b) + y * std::sin(b))});
        }
    };
    if (light.type == LightType::Point) {
        circle(origin, {1, 0, 0}, {0, 1, 0}, light.range);
        circle(origin, {1, 0, 0}, {0, 0, 1}, light.range);
        circle(origin, {0, 1, 0}, {0, 0, 1}, light.range);
    } else if (light.type == LightType::Spot) {
        // Range is radial distance in the shader, not axial cone length.
        for (float angle : {light.innerCutOff, light.outerCutOff}) {
            const float radians = glm::radians(angle);
            const glm::vec3 center = origin + direction * light.range * std::cos(radians);
            const float radius = light.range * std::sin(radians);
            circle(center, right, up, radius);
            for (glm::vec3 axis : {right, -right, up, -up})
                lines.push_back({origin, center + axis * radius});
        }
        lines.push_back({origin, origin + direction * light.range});
    } else {
        // A directional light has no finite range: short parallel arrows show direction.
        for (float offset : {-0.4f, 0.0f, 0.4f}) {
            const glm::vec3 start = origin + right * offset;
            const glm::vec3 end = start + direction * 2.0f;
            lines.push_back({start, end});
            lines.push_back({end, end - direction * 0.3f + up * 0.15f});
            lines.push_back({end, end - direction * 0.3f - up * 0.15f});
        }
    }
    return lines;
}

// Frame the complete transformed bounds, including an off-center mesh pivot,
// using the narrower lens angle so portrait viewports fit the selection too.
inline std::optional<glm::mat4> FrameBounds(const glm::mat4& cameraToWorld,
    const glm::mat4& transform, glm::vec3 minimum, glm::vec3 maximum,
    float verticalFov, float aspect, float nearPlane, float farPlane) {
    if (!editor_camera_gizmo::detail::Finite(cameraToWorld) || !editor_camera_gizmo::detail::Finite(transform) ||
        !std::isfinite(verticalFov) || verticalFov <= 0 || verticalFov >= 179 ||
        !std::isfinite(aspect) || aspect <= 0 || !std::isfinite(nearPlane) || nearPlane <= 0 ||
        !std::isfinite(farPlane) || farPlane <= nearPlane) return std::nullopt;
    glm::vec3 center(0);
    glm::vec3 corners[8];
    for (int axis = 0; axis < 3; ++axis)
        if (!std::isfinite(minimum[axis]) || !std::isfinite(maximum[axis]) || minimum[axis] > maximum[axis])
            return std::nullopt;
    for (int i = 0; i < 8; ++i) {
        corners[i] = glm::vec3(transform * glm::vec4(i & 1 ? maximum.x : minimum.x,
            i & 2 ? maximum.y : minimum.y, i & 4 ? maximum.z : minimum.z, 1));
        center += corners[i] * 0.125f;
    }
    float radius = 0.5f;
    for (const auto& corner : corners) radius = std::max(radius, glm::length(corner - center));
    const float halfY = glm::radians(verticalFov) * 0.5f;
    const float halfX = std::atan(std::tan(halfY) * aspect);
    const float distance = radius / std::sin(std::min(halfX, halfY)) * 1.1f + nearPlane;
    if (!std::isfinite(distance) || distance + radius >= farPlane) return std::nullopt;
    const glm::vec3 back = NormalizeLightDirection(glm::vec3(cameraToWorld[2]));
    const glm::vec3 up = std::abs(back.y) < 0.999f ? glm::vec3(0, 1, 0) : glm::vec3(0, 0, 1);
    return glm::inverse(glm::lookAt(center + back * distance, center, up));
}
}
