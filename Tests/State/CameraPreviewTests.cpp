#include "Runtime/UserState/CameraController.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>

namespace {
    void Require(bool condition, const char* message) {
        if (!condition) {
            std::fprintf(stderr, "FAIL: %s\n", message);
            std::exit(1);
        }
    }

    void Near(float actual, float expected, const char* message) {
        Require(std::isfinite(actual) && std::abs(actual - expected) < 0.0001f, message);
    }

    void SameVector(const glm::vec3& actual, const glm::vec3& expected, const char* message) {
        for (int axis = 0; axis < 3; ++axis) Near(actual[axis], expected[axis], message);
    }

    void SameMatrix(const glm::mat4& actual, const glm::mat4& expected, const char* message) {
        for (int column = 0; column < 4; ++column)
            for (int row = 0; row < 4; ++row) Near(actual[column][row], expected[column][row], message);
    }

    void SameCameraEvolution(const engine::CameraState& actual, const engine::CameraState& expected) {
        Require(actual.mode == expected.mode && actual.thirdPersonMode == expected.thirdPersonMode,
            "preview preserves the camera mode");
        Require(actual.automaticFov == expected.automaticFov && actual.cameraFov == expected.cameraFov &&
            actual.targetFov == expected.targetFov, "preview does not advance or override lens smoothing");
        Require(actual.Yaw == expected.Yaw && actual.targetYaw == expected.targetYaw &&
            actual.Pitch == expected.Pitch && actual.targetPitch == expected.targetPitch &&
            actual.Distance == expected.Distance && actual.targetDistance == expected.targetDistance,
            "preview does not advance orbit smoothing");
        Require(actual.cameraRoll == expected.cameraRoll && actual.targetCameraRoll == expected.targetCameraRoll &&
            actual.cameraIdleTimer == expected.cameraIdleTimer &&
            actual.portalCameraTimer == expected.portalCameraTimer,
            "preview preserves camera roll and timers");
    }

    void TestInitialPoseAndEditedPosition() {
        engine::CameraController camera;
        engine::PlayerState player;
        player.position = {-152.0f, 3.0f, -84.0f};
        const auto original = camera.State();
        Require(camera.RefreshFollowPreview(player), "follow camera accepts an authoring preview");
        SameVector(glm::vec3(camera.State().camera2world[3]), {-151.0f, 3.6f, -79.0f},
            "initial preview is at the player's shoulder and boom, not the default world origin");
        SameVector(-glm::vec3(camera.State().camera2world[2]), {0.0f, 0.0f, -1.0f},
            "initial preview looks down the live camera's negative Z axis");
        SameCameraEvolution(camera.State(), original);

        const auto beforeMove = camera.State();
        const glm::vec3 translation(30.0f, 15.0f, -47.0f);
        player.position += translation;
        Require(camera.RefreshFollowPreview(player), "authoring transform changes can refresh the preview");
        SameVector(glm::vec3(camera.State().camera2world[3]), glm::vec3(beforeMove.camera2world[3]) + translation,
            "moving the authoring player moves the preview by the same world-space delta");
        for (int column = 0; column < 3; ++column)
            SameVector(glm::vec3(camera.State().camera2world[column]), glm::vec3(beforeMove.camera2world[column]),
                "translating the player does not rotate the camera");
        SameCameraEvolution(camera.State(), beforeMove);
        const auto firstPreview = camera.State();
        for (int i = 0; i < 300; ++i) camera.RefreshFollowPreview(player);
        SameMatrix(camera.State().camera2world, firstPreview.camera2world, "preview is idempotent across editor frames");
        SameCameraEvolution(camera.State(), firstPreview);
        std::puts("PASS spawn placement, authoring movement, idempotence and no simulation advancement");
    }

    void TestCurrentOrbitAndPendingTargets() {
        engine::CameraController camera;
        engine::PlayerState player;
        player.position = {10.0f, 20.0f, 30.0f};
        camera.SetOrbit(glm::half_pi<float>(), 0.0f, 8.0f, true);
        camera.SetOrbit(-0.4f, 0.6f, 40.0f, false);
        camera.SetFov(47.0f);
        const auto before = camera.State();
        Require(camera.RefreshFollowPreview(player), "pending targets do not prevent a preview");
        SameVector(glm::vec3(camera.State().camera2world[3]), {18.0f, 20.6f, 29.0f},
            "preview uses current yaw and distance, not pending target values");
        SameVector(-glm::vec3(camera.State().camera2world[2]), {-1.0f, 0.0f, 0.0f},
            "yaw rotates the preview's view direction");
        SameCameraEvolution(camera.State(), before);
        std::puts("PASS current orbit and lens preservation with pending smoothing targets");
    }

    void TestMatchesLiveFollowPoseIncludingRoll() {
        engine::CameraController camera;
        engine::PlayerState player;
        player.position = {11.0f, 7.0f, -28.0f};
        player.isExtremeSpeed = true;
        player.bikeLeanAngle = 0.5f;
        camera.SetOrbit(0.7f, 0.2f, 7.0f, true);
        engine::CameraInput input;
        input.holdAutoAlign = true;
        camera.Update(1.0f / 60.0f, input, player);
        const auto live = camera.State();
        Require(std::abs(live.cameraRoll) > 0.0f, "test setup produces nonzero runtime roll");
        Require(camera.RefreshFollowPreview(player), "runtime follow snapshot can be previewed");
        SameMatrix(camera.State().camera2world, live.camera2world,
            "preview matches the real follow camera pose, including pitch and roll");
        SameCameraEvolution(camera.State(), live);
        std::puts("PASS exact runtime follow pose with nonzero pitch and roll");
    }

    void TestOtherModesAndInvalidPositions() {
        engine::CameraController camera;
        engine::PlayerState player;
        camera.RequestFree();
        camera.SetFreeTransform(glm::translate(glm::mat4(1.0f), glm::vec3(31.0f, 9.0f, 42.0f)));
        auto before = camera.State();
        Require(!camera.RefreshFollowPreview(player), "free camera is not repositioned by authoring preview");
        SameMatrix(camera.State().camera2world, before.camera2world, "free pose is preserved");
        SameCameraEvolution(camera.State(), before);

        camera.BeginCinematic();
        before = camera.State();
        Require(!camera.RefreshFollowPreview(player), "cinematic camera is not overridden");
        SameMatrix(camera.State().camera2world, before.camera2world, "cinematic pose is preserved");
        SameCameraEvolution(camera.State(), before);

        camera.ResetForNewRun();
        camera.BeginPortal(engine::PortalCameraRequest{});
        before = camera.State();
        Require(!camera.RefreshFollowPreview(player), "portal camera is not advanced or handed off");
        SameMatrix(camera.State().camera2world, before.camera2world, "portal pose is preserved");
        SameCameraEvolution(camera.State(), before);
        Require(camera.State().portalCameraActive, "preview does not cancel portal state");

        camera.ResetForNewRun();
        before = camera.State();
        player.position.x = std::numeric_limits<float>::quiet_NaN();
        Require(!camera.RefreshFollowPreview(player), "invalid authoring position is rejected");
        SameMatrix(camera.State().camera2world, before.camera2world, "invalid input preserves the last finite pose");
        std::puts("PASS free/cinematic/portal ownership and invalid position protection");
    }
}

int main() {
    TestInitialPoseAndEditedPosition();
    TestCurrentOrbitAndPendingTargets();
    TestMatchesLiveFollowPoseIncludingRoll();
    TestOtherModesAndInvalidPositions();
    std::puts("All camera preview tests passed.");
    return 0;
}
