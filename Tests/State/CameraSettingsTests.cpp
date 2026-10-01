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
void SameMatrix(const glm::mat4& actual, const glm::mat4& expected, const char* message) {
    for (int column = 0; column < 4; ++column)
        for (int row = 0; row < 4; ++row) Near(actual[column][row], expected[column][row], message);
}
void SameSettingsAndPose(const engine::CameraState& actual, const engine::CameraState& expected) {
    Require(engine::CameraSettings::FromState(actual) == engine::CameraSettings::FromState(expected),
        "rejected edit preserves the previous settings");
    SameMatrix(actual.camera2world, expected.camera2world, "rejected edit preserves pose");
    Near(actual.cameraFov, expected.cameraFov, "rejected edit preserves visible lens");
    Require(actual.mode == expected.mode && actual.portalCameraActive == expected.portalCameraActive,
        "rejected edit preserves temporary ownership");
}

void TestImmediateFollowPreview() {
    engine::CameraController camera;
    engine::CameraSettings settings;
    engine::PlayerState player;
    player.position = {10.0f, 20.0f, 30.0f};
    settings.automaticFov = false;
    settings.fov = 47.0f;
    settings.yaw = glm::half_pi<float>();
    settings.distance = 8.0f;
    settings.nearPlane = 0.3f;
    settings.farPlane = 2400.0f;
    Require(camera.ApplySettings(settings, glm::mat4(1.0f), player), "manual authored settings apply without a simulation step");
    const auto& state = camera.State();
    Near(state.cameraFov, 47.0f, "manual lens is visible immediately while paused");
    Near(state.targetFov, 47.0f, "manual lens has no stale smoothing target");
    Near(state.camera2world[3].x, 18.0f, "orbit yaw and distance immediately move the camera");
    Near(state.camera2world[3].y, 20.6f, "follow preview retains the shoulder offset");
    Near(state.camera2world[3].z, 29.0f, "orbit yaw immediately rotates the shoulder offset");
    Near(state.nearPlane, 0.3f, "authored near plane reaches the published state");
    Near(state.farPlane, 2400.0f, "authored far plane reaches the published state");
    Require(engine::CameraSettings::FromState(state) == settings, "manual initial migration preserves all authoring fields");

    settings.automaticFov = true;
    settings.distance = 70.0f;
    Require(camera.ApplySettings(settings, glm::mat4(1.0f), player), "automatic FOV can be enabled while paused");
    Near(state.cameraFov, 20.0f, "far orbit previews its automatic lens immediately");
    settings.distance = 2.0f;
    Require(camera.ApplySettings(settings, glm::mat4(1.0f), player), "automatic lens recomputes on a distance edit");
    Near(state.cameraFov, 100.0f, "near orbit previews its automatic lens immediately");
    Near(state.targetFov, state.cameraFov, "automatic preview has no lens interpolation lag");
    std::puts("PASS immediate follow orbit, manual/automatic lens and clip preview");
}

void TestValidationAndAtomicRejection() {
    engine::CameraController camera;
    engine::PlayerState player;
    const engine::CameraSettings valid;
    Require(valid.IsValid(), "default settings are valid");
    Require(camera.ApplySettings(valid, glm::mat4(1.0f), player), "valid settings initialize the test");
    const auto before = camera.State();
    const auto reject = [&](const engine::CameraSettings& settings) {
        Require(!settings.IsValid(), "invalid field fails shared validation");
        Require(!camera.ApplySettings(settings, glm::mat4(1.0f), player), "invalid settings are rejected");
        SameSettingsAndPose(camera.State(), before);
    };
    auto settings = valid;
    settings.nearPlane = 0.0f; reject(settings);
    settings = valid; settings.nearPlane = -0.1f; reject(settings);
    settings = valid; settings.nearPlane = 0.0001f; reject(settings);
    settings = valid; settings.farPlane = settings.nearPlane; reject(settings);
    settings = valid; settings.farPlane = std::numeric_limits<float>::infinity(); reject(settings);
    settings = valid; settings.farPlane = 1.0e30f; reject(settings);
    settings = valid; settings.nearPlane = 1.0e29f; settings.farPlane = 1.0e30f; reject(settings);
    settings = valid; settings.fov = 121.0f; reject(settings);
    settings = valid; settings.fov = std::numeric_limits<float>::quiet_NaN(); reject(settings);
    settings = valid; settings.yaw = std::numeric_limits<float>::infinity(); reject(settings);
    settings = valid; settings.yaw = 1.0e30f; reject(settings);
    settings = valid; settings.yaw = -1.0e30f; reject(settings);
    settings = valid; settings.yaw = glm::radians(181.0f); reject(settings);
    settings = valid; settings.pitch = glm::radians(86.0f); reject(settings);
    settings = valid; settings.distance = 0.5f; reject(settings);
    settings = valid; settings.occlusionFadeCoverage = 1.1f; reject(settings);
    settings = valid; settings.occlusionFadeSpeed = 0.0f; reject(settings);
    settings = valid; settings.occlusionTargetHeight = -0.1f; reject(settings);
    settings = valid; settings.mode = engine::CameraMode::Portal; reject(settings);
    settings = valid; settings.mode = engine::CameraMode::Cinematic; reject(settings);
    player.position.z = std::numeric_limits<float>::quiet_NaN();
    Require(!camera.ApplySettings(valid, glm::mat4(1.0f), player), "invalid follow target is rejected atomically");
    SameSettingsAndPose(camera.State(), before);
    auto rotatedState = before;
    rotatedState.targetYaw = glm::radians(810.0f);
    const auto migrated = engine::CameraSettings::FromState(rotatedState);
    Require(migrated.IsValid(), "initial migration wraps accumulated runtime yaw into the authoring range");
    Near(migrated.yaw, glm::half_pi<float>(), "initial migration preserves the wrapped yaw orientation");
    settings = valid;
    settings.nearPlane = 0.001f;
    settings.farPlane = 1000000.0f;
    settings.yaw = glm::pi<float>();
    Require(settings.IsValid(), "authoring limits are inclusive at the documented boundaries");
    std::puts("PASS shared validation and atomic rejection of invalid authoring data");
}

void TestFreePoseAndTemporaryOwnership() {
    engine::CameraController camera;
    engine::PlayerState player;
    engine::CameraSettings settings;
    settings.mode = engine::CameraMode::Free;
    settings.fov = 65.0f;
    const auto pose = glm::translate(glm::mat4(1.0f), glm::vec3(19.0f, 7.0f, -32.0f)) *
        glm::rotate(glm::mat4(1.0f), 0.8f, glm::vec3(0.0f, 1.0f, 0.0f));
    Require(camera.ApplySettings(settings, pose, player), "authored free camera applies");
    SameMatrix(camera.State().camera2world, pose, "free camera retains the authored transform");
    Near(camera.State().cameraFov, 65.0f, "free mode uses the authored lens even if automatic follow FOV is enabled");
    auto before = camera.State();
    Require(!camera.ApplySettings(settings, glm::mat4(0.0f), player), "singular free transform is rejected");
    SameSettingsAndPose(camera.State(), before);
    auto invalidPose = pose;
    invalidPose[0][3] = 1.0f;
    Require(!camera.ApplySettings(settings, invalidPose, player), "non-affine free transform is rejected");
    SameSettingsAndPose(camera.State(), before);
    invalidPose = glm::scale(pose, glm::vec3(2.0f, 1.0f, 1.0f));
    Require(!camera.ApplySettings(settings, invalidPose, player), "scaled free transform is rejected");
    SameSettingsAndPose(camera.State(), before);
    invalidPose = pose;
    invalidPose[1] += 0.2f * invalidPose[0];
    Require(!camera.ApplySettings(settings, invalidPose, player), "sheared free transform is rejected");
    SameSettingsAndPose(camera.State(), before);
    invalidPose = glm::scale(pose, glm::vec3(-1.0f, 1.0f, 1.0f));
    Require(!camera.ApplySettings(settings, invalidPose, player), "reflected free transform is rejected");
    SameSettingsAndPose(camera.State(), before);
    invalidPose = pose;
    invalidPose[3].x = std::numeric_limits<float>::infinity();
    Require(!camera.ApplySettings(settings, invalidPose, player), "non-finite free transform is rejected");
    SameSettingsAndPose(camera.State(), before);

    Require(camera.BeginCinematic(), "cinematic takes control");
    before = camera.State();
    settings.fov = 42.0f;
    settings.nearPlane = 0.6f;
    Require(!camera.ApplySettings(settings, glm::mat4(1.0f), player), "authoring cannot interrupt cinematics");
    SameSettingsAndPose(camera.State(), before);
    camera.EndCinematic();
    Near(camera.State().cameraFov, 65.0f, "rejected cinematic edit does not leak into return state");
    Require(camera.ApplySettings(settings, pose, player), "pending authoring edit can apply after cinematic release");

    engine::PortalCameraRequest portal;
    portal.mappedFollowTarget = {0.0f, 0.0f, -10.0f};
    Require(camera.BeginPortal(portal), "portal takes control");
    before = camera.State();
    settings.mode = engine::CameraMode::Follow;
    Require(!camera.ApplySettings(settings, pose, player), "authoring cannot interrupt a portal traversal");
    SameSettingsAndPose(camera.State(), before);
    camera.CancelPortal();
    Require(camera.ApplySettings(settings, pose, player), "pending edit applies after portal release");
    Require(camera.State().mode == engine::CameraMode::Follow, "authored mode applies after temporary ownership ends");
    std::puts("PASS free transforms and deferred edits during cinematic/portal ownership");
}

void TestAuthoredSettingsSurviveSimulationAndReset() {
    engine::CameraController camera;
    engine::PlayerState player;
    engine::CameraSettings settings;
    settings.nearPlane = 0.25f;
    settings.farPlane = 2500.0f;
    settings.occlusionFadeEnabled = false;
    settings.occlusionFadeCoverage = 0.6f;
    settings.occlusionFadeSpeed = 12.0f;
    settings.occlusionTargetHeight = 1.8f;
    const auto authored = settings;
    Require(camera.ApplySettings(settings, glm::mat4(1.0f), player), "authored settings apply before simulation");
    player.isExtremeSpeed = true;
    player.bikeSpeed = 45.0f;
    player.bikeLeanAngle = 0.3f;
    for (int frame = 0; frame < 60; ++frame) camera.Update(1.0f / 60.0f, engine::CameraInput{}, player);
    Require(camera.State().Distance < 2.0f && camera.State().cameraFov > 110.0f,
        "gameplay can freely evolve the runtime camera");
    Require(settings == authored, "simulation cannot overwrite persisted authoring settings");
    camera.ResetFollow();
    Near(camera.State().nearPlane, settings.nearPlane, "follow reset retains authored near plane");
    Near(camera.State().farPlane, settings.farPlane, "follow reset retains authored far plane");
    Require(camera.State().occlusionFadeEnabled == settings.occlusionFadeEnabled,
        "follow reset retains authored occlusion toggle");
    Near(camera.State().occlusionFadeCoverage, settings.occlusionFadeCoverage, "follow reset retains occlusion coverage");
    Near(camera.State().occlusionFadeSpeed, settings.occlusionFadeSpeed, "follow reset retains occlusion response");
    Near(camera.State().occlusionTargetHeight, settings.occlusionTargetHeight, "follow reset retains occlusion target height");
    Require(camera.ApplySettings(settings, glm::mat4(1.0f), player), "a new authoring preview restores the saved settings");
    Near(camera.State().Distance, 5.0f, "reapplying authored settings clears the simulated extreme-speed distance");
    Near(camera.State().cameraRoll, 0.0f, "authoring preview clears runtime lean");
    std::puts("PASS authored/runtime separation and lens/occlusion preservation on follow reset");
}
} // namespace

int main() {
    TestImmediateFollowPreview();
    TestValidationAndAtomicRejection();
    TestFreePoseAndTemporaryOwnership();
    TestAuthoredSettingsSurviveSimulationAndReset();
    std::puts("All camera settings tests passed.");
    return 0;
}
