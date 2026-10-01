#include "Runtime/Scene/GameCameraEntity.hpp"
#include <cstdio>
#include <cstdlib>
#include <cmath>

using namespace engine;

static void Require(bool condition, const char* message) {
    if (!condition) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}

static void SameMatrix(const glm::mat4& a, const glm::mat4& b, const char* message) {
    for (int col = 0; col < 4; ++col)
        for (int row = 0; row < 4; ++row)
            Require(std::abs(a[col][row] - b[col][row]) < 0.0001f, message);
}

int main() {
    flecs::world authoringWorld;
    CameraController camera;
    PlayerState player;
    player.position = {-152, 3, -84};
    camera.RefreshFollowPreview(player);
    Require(!IsEntityAlive(FindGameCameraEntity(authoringWorld)), "absent camera lookup is safe");
    const auto entity = EnsureGameCameraEntity(authoringWorld, camera);
    Require(IsEntityAlive(entity) && entity.has<LocalTransform>() && entity.has<WorldTransform>(),
        "camera is a real scene entity with local/world transforms");
    Require(EnsureGameCameraEntity(authoringWorld, camera).id() == entity.id(), "initialization is idempotent");
    Require(!ApplyGameCameraEdits(authoringWorld, camera, player), "creation does not reset live camera");

    auto& edit = entity.get_mut<GameCameraComponent>();
    edit.settings.automaticFov = false;
    edit.settings.fov = 47;
    edit.settings.distance = 12;
    edit.settings.yaw = 0.7f;
    edit.settings.nearPlane = 0.3f;
    edit.settings.farPlane = 420;
    edit.settingsDirty = true;
    Require(ApplyGameCameraEdits(authoringWorld, camera, player), "pending edit applies without a simulation step");
    Require(camera.State().cameraFov == 47 && camera.State().Distance == 12 &&
        camera.State().nearPlane == 0.3f && camera.State().farPlane == 420, "authored lens/orbit reaches runtime");
    Require(!entity.get<GameCameraComponent>().settingsDirty, "accepted edit is consumed once");
    const auto authored = entity.get<GameCameraComponent>();
    CameraInput input;
    input.lookDelta = {0.1f, 0.05f};
    input.distanceDelta = 3;
    for (int i = 0; i < 20; ++i) camera.Update(1.0f / 60, input, player);
    PublishGameCameraPose(authoringWorld, camera);
    Require(entity.get<GameCameraComponent>().settings == authored.settings,
        "gameplay input cannot overwrite scene-authored settings");
    SameMatrix(entity.get<WorldTransform>().matrix, camera.State().camera2world,
        "scene entity follows the actual runtime camera");
    SameMatrix(entity.get<GameCameraComponent>().freeTransform, authored.freeTransform,
        "runtime publication preserves the saved free-camera pose");
    const auto live = camera.State();
    Require(!ApplyGameCameraEdits(authoringWorld, camera, player), "unchanged entity does not reapply settings");
    SameMatrix(camera.State().camera2world, live.camera2world, "no edit leaves smoothing pose untouched");

    camera.BeginCinematic();
    const auto shot = glm::translate(glm::mat4(1), glm::vec3(10, 20, 30));
    camera.SetCinematicTransform(shot);
    entity.get_mut<GameCameraComponent>().settings.fov = 61;
    entity.get_mut<GameCameraComponent>().settingsDirty = true;
    Require(!ApplyGameCameraEdits(authoringWorld, camera, player), "authoring cannot interrupt a cinematic");
    SameMatrix(camera.State().camera2world, shot, "cinematic pose remains owned by gameplay");
    Require(entity.get<GameCameraComponent>().settingsDirty, "deferred edit remains pending");
    camera.EndCinematic();
    Require(ApplyGameCameraEdits(authoringWorld, camera, player), "edit applies after temporary owner releases camera");

    auto& freeEdit = entity.get_mut<GameCameraComponent>();
    freeEdit.settings.mode = CameraMode::Free;
    freeEdit.freeTransform = shot;
    freeEdit.settingsDirty = true;
    Require(ApplyGameCameraEdits(authoringWorld, camera, player), "free pose applies without simulation");
    SameMatrix(camera.State().camera2world, shot, "free pose matches authored transform");
    input = {};
    input.localMove = {2, 0, 0};
    camera.Update(0.05f, input, player);
    PublishGameCameraPose(authoringWorld, camera);
    SameMatrix(entity.get<GameCameraComponent>().freeTransform, shot, "free-fly runtime input does not alter authored transform");

    flecs::world playWorld;
    CameraController playCamera;
    const auto playEntity = EnsureGameCameraEntity(playWorld, playCamera);
    CopyGameCameraAuthoring(entity, playEntity);
    Require(ApplyGameCameraEdits(playWorld, playCamera, player), "Play copies unsaved camera settings");
    SameMatrix(playCamera.State().camera2world, shot, "Play starts at the authored free pose, not drifted runtime pose");
    playEntity.get_mut<GameCameraComponent>().settings.fov = 90;
    Require(entity.get<GameCameraComponent>().settings.fov == 61, "Play world cannot mutate retained authoring world");

    entity.destruct();
    Require(!ApplyGameCameraEdits(authoringWorld, camera, player), "missing entity update is safe");
    PublishGameCameraPose(authoringWorld, camera);
    const auto replacement = EnsureGameCameraEntity(authoringWorld, camera);
    Require(IsEntityAlive(replacement) && replacement.id() != entity.id(), "world lifecycle does not retain stale handles");
    std::puts("PASS real Flecs camera entity lifecycle, authoring/runtime isolation, deferred takeover edits and Play copy");
}
