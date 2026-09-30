#pragma once

#include <glm/glm.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <optional>

namespace engine::editor_camera_gizmo {

struct CameraView {
    glm::mat4 cameraToWorld{1.0f};
    float verticalFovDegrees = 60.0f;
    float aspectRatio = 16.0f / 9.0f;
    float nearPlane = 0.1f;
    float farPlane = 1000.0f;
};

struct FrustumDisplaySettings {
    float previewDistance = 5.0f;
    bool useCameraFarClip = false;
};

namespace detail {

inline bool Finite(const glm::dvec4& value) {
    return std::isfinite(value.x) && std::isfinite(value.y)
        && std::isfinite(value.z) && std::isfinite(value.w);
}

inline bool Finite(const glm::mat4& value) {
    for (int column = 0; column < 4; ++column)
        for (int row = 0; row < 4; ++row)
            if (!std::isfinite(value[column][row])) return false;
    return true;
}

// Clip before dividing by w. ZO projection uses 0 <= z <= w, unlike
// OpenGL's -w <= z <= w. The final plane avoids division at the eye.
inline std::array<double, 7> PlaneDistances(const glm::dvec4& point) {
    return {point.w + point.x, point.w - point.x,
        point.w + point.y, point.w - point.y,
        point.z, point.w - point.z, point.w - 1e-7};
}

inline glm::vec2 ToNdc(const glm::dvec4& point) {
    // The segment has already been clipped; clamp floating point roundoff
    // at the boundary so ImGui never receives an unbounded screen coordinate.
    return {static_cast<float>(std::clamp(point.x / point.w, -1.0, 1.0)),
        static_cast<float>(std::clamp(point.y / point.w, -1.0, 1.0))};
}

} // namespace detail

// RH camera pose, looking down local -Z with +Y up. Corners 0..3 are the
// near plane (bottom-left, bottom-right, top-right, top-left); 4..7 are the
// matching far corners. Use the supplied lens exactly; display-only shortening
// belongs to MakeFrustumDisplayView rather than this geometry routine.
inline std::optional<std::array<glm::vec3, 8>> BuildFrustumCorners(const CameraView& view) {
    if (!detail::Finite(view.cameraToWorld)
        || !std::isfinite(view.verticalFovDegrees) || view.verticalFovDegrees <= 0.0f || view.verticalFovDegrees >= 180.0f
        || !std::isfinite(view.aspectRatio) || view.aspectRatio <= 0.0f
        || !std::isfinite(view.nearPlane) || view.nearPlane <= 0.0f
        || !std::isfinite(view.farPlane) || view.farPlane <= view.nearPlane)
        return std::nullopt;

    const double halfAngle = static_cast<double>(view.verticalFovDegrees) * 0.0087266462599716478846;
    const double slope = std::tan(halfAngle);
    const glm::dmat4 cameraToWorld(view.cameraToWorld);
    constexpr std::array<glm::dvec2, 4> signs = {
        glm::dvec2{-1.0, -1.0}, glm::dvec2{1.0, -1.0},
        glm::dvec2{1.0, 1.0}, glm::dvec2{-1.0, 1.0}};
    std::array<glm::vec3, 8> corners{};
    for (std::size_t plane = 0; plane < 2; ++plane) {
        const double distance = plane == 0 ? view.nearPlane : view.farPlane;
        const double halfHeight = distance * slope;
        const double halfWidth = halfHeight * view.aspectRatio;
        for (std::size_t index = 0; index < signs.size(); ++index) {
            const glm::dvec4 world = cameraToWorld * glm::dvec4(
                signs[index].x * halfWidth, signs[index].y * halfHeight, -distance, 1.0);
            if (!detail::Finite(world) || std::abs(world.w) < 1e-7) return std::nullopt;
            const glm::dvec3 position = glm::dvec3(world) / world.w;
            for (int axis = 0; axis < 3; ++axis)
                if (!std::isfinite(position[axis]) || std::abs(position[axis]) > std::numeric_limits<float>::max())
                    return std::nullopt;
            corners[plane * 4 + index] = glm::vec3(position);
        }
    }
    return corners;
}

// Make a display-only lens; the camera's rendering range is never changed.
// A short preview preserves the near plane and opening angles while avoiding
// kilometer-long overlay edges. Full mode ignores the preview distance.
inline std::optional<CameraView> MakeFrustumDisplayView(
    const CameraView& camera, const FrustumDisplaySettings& settings) {
    if (!BuildFrustumCorners(camera)) return std::nullopt;
    if (settings.useCameraFarClip) return camera;
    if (!std::isfinite(settings.previewDistance) || settings.previewDistance <= 0.0f)
        return std::nullopt;

    CameraView display = camera;
    double distance = settings.previewDistance;
    if (distance <= camera.nearPlane) {
        const double nearPlane = camera.nearPlane;
        distance = nearPlane + std::max(nearPlane * 0.1, 1.0);
    }
    display.farPlane = static_cast<float>(std::min(distance, static_cast<double>(camera.farPlane)));
    return display;
}

// Returns a visible NDC point for a non-Y-flipped RH_ZO view projection.
inline std::optional<glm::vec2> ProjectPoint(const glm::mat4& viewProjection, const glm::vec3& point) {
    if (!detail::Finite(viewProjection)) return std::nullopt;
    const glm::dvec4 clip = glm::dmat4(viewProjection) * glm::dvec4(point, 1.0);
    if (!detail::Finite(clip)) return std::nullopt;
    for (double distance : detail::PlaneDistances(clip))
        if (distance < 0.0) return std::nullopt;
    return detail::ToNdc(clip);
}

// Homogeneous parametric clipping preserves segments that cross the near
// plane or enter the viewport even when neither original endpoint is visible.
// Outputs are untouched on rejection.
inline bool ProjectSegment(const glm::mat4& viewProjection,
    const glm::vec3& start, const glm::vec3& end, glm::vec2& ndcStart, glm::vec2& ndcEnd) {
    if (!detail::Finite(viewProjection)) return false;
    const glm::dmat4 projection(viewProjection);
    const glm::dvec4 clipStart = projection * glm::dvec4(start, 1.0);
    const glm::dvec4 clipEnd = projection * glm::dvec4(end, 1.0);
    if (!detail::Finite(clipStart) || !detail::Finite(clipEnd)) return false;
    const auto startDistances = detail::PlaneDistances(clipStart);
    const auto endDistances = detail::PlaneDistances(clipEnd);
    double enter = 0.0;
    double leave = 1.0;
    for (std::size_t plane = 0; plane < startDistances.size(); ++plane) {
        const double a = startDistances[plane];
        const double b = endDistances[plane];
        if (a < 0.0 && b < 0.0) return false;
        if (a >= 0.0 && b >= 0.0) continue;
        const double intersection = a / (a - b);
        if (a < 0.0) enter = std::max(enter, intersection);
        else leave = std::min(leave, intersection);
        if (enter > leave) return false;
    }
    const glm::dvec4 delta = clipEnd - clipStart;
    const glm::dvec4 clippedStart = clipStart + delta * enter;
    const glm::dvec4 clippedEnd = clipStart + delta * leave;
    if (!detail::Finite(clippedStart) || !detail::Finite(clippedEnd)
        || clippedStart.w <= 0.0 || clippedEnd.w <= 0.0)
        return false;
    ndcStart = detail::ToNdc(clippedStart);
    ndcEnd = detail::ToNdc(clippedEnd);
    return true;
}

} // namespace engine::editor_camera_gizmo
