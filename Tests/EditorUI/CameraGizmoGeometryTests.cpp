#include "Runtime/UI/CameraGizmoGeometry.hpp"

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace gizmo = engine::editor_camera_gizmo;

namespace {

void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void Near(float actual, float expected, float tolerance = 1e-4f) {
    Require(std::isfinite(actual) && std::abs(actual - expected) < tolerance, "unexpected geometry coordinate");
}

void Near(const glm::vec2& actual, const glm::vec2& expected) {
    Near(actual.x, expected.x);
    Near(actual.y, expected.y);
}

void Near(const glm::vec3& actual, const glm::vec3& expected) {
    Near(actual.x, expected.x);
    Near(actual.y, expected.y);
    Near(actual.z, expected.z);
}

glm::mat4 Projection() {
    return glm::perspectiveRH_ZO(glm::radians(90.0f), 1.0f, 1.0f, 11.0f);
}

void FrustumDimensionsAndFarDistance() {
    gizmo::CameraView view;
    view.verticalFovDegrees = 90.0f;
    view.aspectRatio = 2.0f;
    view.nearPlane = 1.0f;
    view.farPlane = 10000.0f;
    const auto result = gizmo::BuildFrustumCorners(view);
    Require(result.has_value(), "valid camera must produce a frustum");
    Near((*result)[0], {-2.0f, -1.0f, -1.0f});
    Near((*result)[1], {2.0f, -1.0f, -1.0f});
    Near((*result)[2], {2.0f, 1.0f, -1.0f});
    Near((*result)[3], {-2.0f, 1.0f, -1.0f});
    Near((*result)[4], {-20000.0f, -10000.0f, -10000.0f});
    Near((*result)[6], {20000.0f, 10000.0f, -10000.0f});

    view.verticalFovDegrees = 60.0f;
    view.aspectRatio = 1.0f;
    const auto narrow = gizmo::BuildFrustumCorners(view);
    Require(narrow.has_value(), "narrow FOV must produce a frustum");
    Near((*narrow)[2], {0.577350269f, 0.577350269f, -1.0f});
}

void CameraPoseAndProjectionAgreement() {
    gizmo::CameraView view;
    view.verticalFovDegrees = 70.0f;
    view.aspectRatio = 16.0f / 9.0f;
    view.nearPlane = 0.5f;
    view.farPlane = 50.0f;
    view.cameraToWorld = glm::translate(glm::mat4(1.0f), {3.0f, 4.0f, 5.0f})
        * glm::rotate(glm::mat4(1.0f), glm::radians(90.0f), {0.0f, 1.0f, 0.0f});
    const auto result = gizmo::BuildFrustumCorners(view);
    Require(result.has_value(), "transformed camera must produce a frustum");
    Near(((*result)[0] + (*result)[2]) * 0.5f, {2.5f, 4.0f, 5.0f});
    Near(((*result)[4] + (*result)[6]) * 0.5f, {-47.0f, 4.0f, 5.0f});

    const auto projection = glm::perspectiveRH_ZO(glm::radians(view.verticalFovDegrees),
        view.aspectRatio, view.nearPlane, view.farPlane) * glm::inverse(view.cameraToWorld);
    const glm::vec2 expected[] = {{-1, -1}, {1, -1}, {1, 1}, {-1, 1}};
    for (std::size_t index = 0; index < result->size(); ++index) {
        const glm::vec4 clip = projection * glm::vec4((*result)[index], 1.0f);
        Near(glm::vec2(clip) / clip.w, expected[index % 4]);
        Near(clip.z / clip.w, index < 4 ? 0.0f : 1.0f);
    }
}

void RejectInvalidCameras() {
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    for (float fov : {-1.0f, 0.0f, 180.0f, 200.0f, nan, inf}) {
        gizmo::CameraView view;
        view.verticalFovDegrees = fov;
        Require(!gizmo::BuildFrustumCorners(view), "invalid FOV must be rejected");
    }
    for (float aspect : {-1.0f, 0.0f, nan, inf}) {
        gizmo::CameraView view;
        view.aspectRatio = aspect;
        Require(!gizmo::BuildFrustumCorners(view), "invalid aspect must be rejected");
    }
    for (float nearPlane : {-1.0f, 0.0f, 1000.0f, 1001.0f, nan, inf}) {
        gizmo::CameraView view;
        view.nearPlane = nearPlane;
        Require(!gizmo::BuildFrustumCorners(view), "invalid near plane must be rejected");
    }
    for (float farPlane : {-1.0f, 0.0f, 0.1f, nan, inf}) {
        gizmo::CameraView view;
        view.farPlane = farPlane;
        Require(!gizmo::BuildFrustumCorners(view), "invalid far plane must be rejected");
    }
    gizmo::CameraView view;
    view.cameraToWorld[0][0] = nan;
    Require(!gizmo::BuildFrustumCorners(view), "nonfinite pose must be rejected");
    view.cameraToWorld = glm::mat4(0.0f);
    Require(!gizmo::BuildFrustumCorners(view), "zero homogeneous transform must be rejected");
    view.cameraToWorld = glm::mat4(1.0f);
    view.farPlane = std::numeric_limits<float>::max();
    view.verticalFovDegrees = 179.0f;
    Require(!gizmo::BuildFrustumCorners(view), "unrepresentable corners must be rejected");
}

void PointVisibility() {
    const auto projection = Projection();
    const auto center = gizmo::ProjectPoint(projection, {0.0f, 0.0f, -2.0f});
    Require(center.has_value(), "point inside frustum must be visible");
    Near(*center, {0.0f, 0.0f});
    const auto upperRight = gizmo::ProjectPoint(projection, {1.0f, 0.5f, -2.0f});
    Require(upperRight.has_value(), "upper right point must be visible");
    Near(*upperRight, {0.5f, 0.25f});
    for (const glm::vec3& outside : {glm::vec3{0, 0, 2}, glm::vec3{0, 0, 0},
        glm::vec3{0, 0, -0.5f}, glm::vec3{0, 0, -12},
        glm::vec3{-3, 0, -2}, glm::vec3{3, 0, -2},
        glm::vec3{0, -3, -2}, glm::vec3{0, 3, -2}})
        Require(!gizmo::ProjectPoint(projection, outside), "out-of-frustum point must be hidden");
}

void SegmentsClipAllPlanes() {
    glm::vec2 start, end;
    const auto projection = Projection();
    Require(gizmo::ProjectSegment(projection, {-3, 0, -2}, {3, 0, -2}, start, end), "left/right crossing must survive");
    Near(start, {-1, 0});
    Near(end, {1, 0});
    Require(gizmo::ProjectSegment(projection, {0, -3, -2}, {0, 3, -2}, start, end), "bottom/top crossing must survive");
    Near(start, {0, -1});
    Near(end, {0, 1});
    Require(gizmo::ProjectSegment(projection, {0, 0, -0.5f}, {1, 0, -2}, start, end), "near crossing must survive");
    Near(start, {1.0f / 3.0f, 0});
    Near(end, {0.5f, 0});
    const glm::mat4 identity(1.0f);
    Require(gizmo::ProjectSegment(identity, {0, 0, -1}, {0.5f, 0, 1}, start, end), "ZO near plane is z=0");
    Near(start, {0.25f, 0});
    Near(end, {0.5f, 0});
    Require(gizmo::ProjectSegment(identity, {0, 0, 0.5f}, {0.75f, 0, 2}, start, end), "far crossing must survive");
    Near(start, {0, 0});
    Near(end, {0.25f, 0});
}

void BehindEyeAndRejectedSegments() {
    glm::vec2 start, end;
    const auto projection = Projection();
    Require(gizmo::ProjectSegment(projection, {0, 0, 2}, {1, 0, -2}, start, end), "segment behind eye entering near plane must survive");
    Near(start, {0.75f, 0});
    Near(end, {0.5f, 0});
    glm::vec2 reverseStart, reverseEnd;
    Require(gizmo::ProjectSegment(projection, {1, 0, -2}, {0, 0, 2}, reverseStart, reverseEnd), "reversed segment must survive");
    Near(reverseStart, end);
    Near(reverseEnd, start);

    start = {123, 456};
    end = {789, 100};
    Require(!gizmo::ProjectSegment(projection, {0, 0, 2}, {1, 0, 3}, start, end), "both behind eye must reject");
    Require(!gizmo::ProjectSegment(projection, {-4, 0, -2}, {-3, 0, -2}, start, end), "both outside same side must reject");
    Require(!gizmo::ProjectSegment(projection, {0, 0, -12}, {1, 0, -20}, start, end), "both beyond far plane must reject");
    Require(!gizmo::ProjectSegment(projection, {0, 0, -0.5f}, {0.1f, 0, -0.25f}, start, end), "both before near plane must reject");
    // Endpoints are outside different planes, but the line misses the corner.
    Require(!gizmo::ProjectSegment(glm::mat4(1), {-2, 0.5f, 0.5f}, {-0.5f, 2, 0.5f}, start, end), "empty intersection must reject");
    Near(start, {123, 456});
    Near(end, {789, 100});
}

void DegenerateAndExtremeSegments() {
    glm::vec2 start, end;
    Require(gizmo::ProjectSegment(Projection(), {0, 0, -2}, {0, 0, -2}, start, end), "visible zero-length segment is valid");
    Near(start, {0, 0});
    Near(end, {0, 0});
    Require(!gizmo::ProjectSegment(Projection(), {0, 0, 0}, {0, 0, 0}, start, end), "eye point must reject");
    Require(gizmo::ProjectSegment(Projection(), {-1e6f, 0, -2}, {1e6f, 0, -2}, start, end), "large side crossing must clip");
    Near(start, {-1, 0});
    Near(end, {1, 0});
    Require(gizmo::ProjectSegment(glm::mat4(1), {-1, -1, 0}, {1, 1, 1}, start, end), "boundary endpoints must survive");
    Near(start, {-1, -1});
    Near(end, {1, 1});
}

void NonfiniteProjectionInputs() {
    glm::vec2 start, end;
    for (float invalid : {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity()}) {
        const glm::vec3 point{invalid, 0, -2};
        Require(!gizmo::ProjectPoint(Projection(), point), "nonfinite point must reject");
        Require(!gizmo::ProjectSegment(Projection(), point, {0, 0, -2}, start, end), "nonfinite segment start must reject");
        Require(!gizmo::ProjectSegment(Projection(), {0, 0, -2}, point, start, end), "nonfinite segment end must reject");
        auto projection = Projection();
        projection[0][0] = invalid;
        Require(!gizmo::ProjectPoint(projection, {0, 0, -2}), "nonfinite projection must reject point");
        Require(!gizmo::ProjectSegment(projection, {0, 0, -2}, {1, 0, -2}, start, end), "nonfinite projection must reject segment");
    }
    Require(!gizmo::ProjectPoint(glm::mat4(0), {0, 0, -2}), "zero homogeneous projection must reject point");
    Require(!gizmo::ProjectSegment(glm::mat4(0), {0, 0, -2}, {1, 0, -2}, start, end), "zero homogeneous projection must reject segment");
}

void DefaultPreviewPreservesCameraAndAngles() {
    gizmo::CameraView camera;
    camera.verticalFovDegrees = 85.0f;
    camera.aspectRatio = 1.42f;
    camera.cameraToWorld = glm::translate(glm::mat4(1), {3, 4, 5})
        * glm::rotate(glm::mat4(1), glm::radians(30.0f), {0, 1, 0});
    const auto before = camera;
    const auto display = gizmo::MakeFrustumDisplayView(camera, {});
    Require(display.has_value(), "default preview must be valid");
    Near(display->farPlane, 5.0f);
    Near(camera.farPlane, 1000.0f);
    Near(display->nearPlane, camera.nearPlane);
    Near(display->verticalFovDegrees, camera.verticalFovDegrees);
    Near(display->aspectRatio, camera.aspectRatio);
    for (int column = 0; column < 4; ++column)
        for (int row = 0; row < 4; ++row) {
            Near(camera.cameraToWorld[column][row], before.cameraToWorld[column][row]);
            Near(display->cameraToWorld[column][row], camera.cameraToWorld[column][row]);
        }
    Near(camera.nearPlane, before.nearPlane);
    Near(camera.verticalFovDegrees, before.verticalFovDegrees);
    Near(camera.aspectRatio, before.aspectRatio);

    const auto originalCorners = gizmo::BuildFrustumCorners(camera);
    const auto previewCorners = gizmo::BuildFrustumCorners(*display);
    Require(originalCorners && previewCorners, "both lenses must build valid geometry");
    const glm::vec3 position(camera.cameraToWorld[3]);
    for (std::size_t index = 0; index < 4; ++index) {
        Near((*previewCorners)[index], (*originalCorners)[index]);
        Near(glm::normalize((*previewCorners)[index + 4] - position),
            glm::normalize((*originalCorners)[index + 4] - position));
    }
}

void PreviewRangeAndNearPlaneLimits() {
    gizmo::CameraView camera;
    camera.farPlane = 3.0f;
    auto display = gizmo::MakeFrustumDisplayView(camera, {});
    Require(display.has_value(), "camera range shorter than preview must be valid");
    Near(display->farPlane, 3.0f);

    camera.nearPlane = 5.0f;
    camera.farPlane = 1000.0f;
    display = gizmo::MakeFrustumDisplayView(camera, {});
    Require(display.has_value(), "near plane at preview distance must be valid");
    Near(display->nearPlane, 5.0f);
    Near(display->farPlane, 6.0f);

    camera.nearPlane = 20.0f;
    display = gizmo::MakeFrustumDisplayView(camera, {});
    Require(display.has_value(), "large near plane must keep positive preview depth");
    Near(display->farPlane, 22.0f);

    camera.farPlane = 21.0f;
    display = gizmo::MakeFrustumDisplayView(camera, {});
    Require(display.has_value(), "near margin must clamp to actual far plane");
    Near(display->farPlane, 21.0f);
    camera.farPlane = std::nextafter(camera.nearPlane, std::numeric_limits<float>::infinity());
    display = gizmo::MakeFrustumDisplayView(camera, {});
    Require(display && display->farPlane > display->nearPlane, "narrowest representable camera range must remain valid");

    camera.nearPlane = 0.1f;
    camera.farPlane = 1000.0f;
    display = gizmo::MakeFrustumDisplayView(camera, {12.0f, false});
    Require(display.has_value(), "explicit preview distance must be honored");
    Near(display->farPlane, 12.0f);
}

void InvalidDisplaySettingsAndCamera() {
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    for (float distance : {-1.0f, 0.0f, nan, inf})
        Require(!gizmo::MakeFrustumDisplayView({}, {distance, false}), "invalid preview distance must reject");

    gizmo::CameraView camera;
    camera.farPlane = camera.nearPlane;
    Require(!gizmo::MakeFrustumDisplayView(camera, {}), "preview must not repair invalid camera range");
    Require(!gizmo::MakeFrustumDisplayView(camera, {nan, true}), "full mode must reject invalid camera range");
    camera = {};
    camera.verticalFovDegrees = nan;
    Require(!gizmo::MakeFrustumDisplayView(camera, {}), "preview must reject invalid camera FOV");
    camera = {};
    camera.cameraToWorld[3][0] = inf;
    Require(!gizmo::MakeFrustumDisplayView(camera, {}), "preview must reject invalid camera pose");
}

void FullDisplayUsesActualFarClip() {
    gizmo::CameraView camera;
    camera.verticalFovDegrees = 90.0f;
    camera.aspectRatio = 1.0f;
    for (float distance : {5.0f, 0.0f, -1.0f, std::numeric_limits<float>::quiet_NaN(),
        std::numeric_limits<float>::infinity()}) {
        const auto display = gizmo::MakeFrustumDisplayView(camera, {distance, true});
        Require(display.has_value(), "full mode must ignore preview distance");
        Near(display->farPlane, 1000.0f);
        const auto corners = gizmo::BuildFrustumCorners(*display);
        Require(corners.has_value(), "full mode frustum must be valid");
        Near((*corners)[6], {1000, 1000, -1000});
    }
}

void SpawnViewContainsDefaultPreviewEdges() {
    gizmo::CameraView camera;
    camera.cameraToWorld = glm::translate(glm::mat4(1), {-151.0f, 3.6f, -79.0f});
    camera.verticalFovDegrees = 85.0f;
    camera.aspectRatio = 1.42f;
    const auto display = gizmo::MakeFrustumDisplayView(camera, {});
    Require(display.has_value(), "spawn preview must be valid");
    const auto corners = gizmo::BuildFrustumCorners(*display);
    Require(corners.has_value(), "spawn preview must build");
    const auto sceneProjection = glm::perspectiveRH_ZO(glm::radians(75.0f), 1.42f, 0.1f, 1000.0f)
        * glm::lookAtRH(glm::vec3{-145, 10, -72}, glm::vec3{-152, 3, -84}, glm::vec3{0, 1, 0});
    constexpr int edges[12][2] = {
        {0, 1}, {1, 2}, {2, 3}, {3, 0}, {4, 5}, {5, 6}, {6, 7}, {7, 4},
        {0, 4}, {1, 5}, {2, 6}, {3, 7}};
    for (const auto& edge : edges) {
        const auto originalStart = gizmo::ProjectPoint(sceneProjection, (*corners)[edge[0]]);
        const auto originalEnd = gizmo::ProjectPoint(sceneProjection, (*corners)[edge[1]]);
        Require(originalStart && originalEnd, "every preview edge endpoint must be inside the spawn viewport");
        glm::vec2 start, end;
        Require(gizmo::ProjectSegment(sceneProjection, (*corners)[edge[0]], (*corners)[edge[1]], start, end),
            "all 12 preview edges must survive at spawn");
        Near(start, *originalStart);
        Near(end, *originalEnd);
    }
}

} // namespace

int main() {
    struct Test { const char* name; void (*run)(); };
    const Test tests[] = {
        {"Frustum dimensions and real far distance", FrustumDimensionsAndFarDistance},
        {"Camera pose and RH_ZO projection agreement", CameraPoseAndProjectionAgreement},
        {"Invalid camera parameters", RejectInvalidCameras},
        {"Point visibility", PointVisibility},
        {"All six clip planes", SegmentsClipAllPlanes},
        {"Behind-eye and rejected segments", BehindEyeAndRejectedSegments},
        {"Degenerate and extreme segments", DegenerateAndExtremeSegments},
        {"Nonfinite projection inputs", NonfiniteProjectionInputs},
        {"Default preview preserves camera and angles", DefaultPreviewPreservesCameraAndAngles},
        {"Preview range and near plane limits", PreviewRangeAndNearPlaneLimits},
        {"Invalid display settings and camera", InvalidDisplaySettingsAndCamera},
        {"Full display uses actual far clip", FullDisplayUsesActualFarClip},
        {"Spawn view contains all default preview edges", SpawnViewContainsDefaultPreviewEdges}
    };
    try {
        for (const auto& test : tests) {
            test.run();
            std::cout << "PASS: " << test.name << '\n';
        }
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
    std::cout << "All 13 camera gizmo geometry test groups passed.\n";
    return 0;
}
