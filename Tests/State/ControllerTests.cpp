#include "Runtime/UserState/UserState.hpp"
#include "Runtime/UserState/StateViews.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <string>
#include <type_traits>
#include <utility>

using namespace engine;

static_assert(std::is_same_v<decltype(std::declval<PlayerController&>().State()), const PlayerState&>);
static_assert(std::is_same_v<decltype(std::declval<CameraController&>().State()), const CameraState&>);
static_assert(!std::is_invocable_v<decltype(&PlayerController::Die), decltype(std::declval<RendererStateView>().player)>);
static_assert(!std::is_invocable_v<decltype(&PlayerController::Die), decltype(std::declval<RuntimeUiStateView>().player)>);

namespace {
    constexpr float kDt = 1.0f / 60.0f;

    void Require(bool condition, const std::string& message) {
        if (!condition) {
            std::fprintf(stderr, "FAIL: %s\n", message.c_str());
            std::exit(1);
        }
    }
    void Near(float actual, float expected, float tolerance, const char* message) {
        Require(std::isfinite(actual) && std::abs(actual - expected) <= tolerance,
            std::string(message) + " actual=" + std::to_string(actual) + " expected=" + std::to_string(expected));
    }
    void SameVector(const glm::vec3& actual, const glm::vec3& expected, const char* message) {
        for (int axis = 0; axis < 3; ++axis) Near(actual[axis], expected[axis], 0.0001f, message);
    }
    void SameMatrix(const glm::mat4& actual, const glm::mat4& expected, const char* message) {
        for (int column = 0; column < 4; ++column)
            for (int row = 0; row < 4; ++row) Near(actual[column][row], expected[column][row], 0.0001f, message);
    }
    void FiniteCamera(const CameraController& camera) {
        const auto& state = camera.State();
        for (int column = 0; column < 4; ++column)
            for (int row = 0; row < 4; ++row)
                Require(std::isfinite(state.camera2world[column][row]), "camera transform stays finite");
        Require(std::isfinite(state.cameraFov) && std::isfinite(state.targetFov) &&
            std::isfinite(state.Yaw) && std::isfinite(state.Pitch) && std::isfinite(state.Distance),
            "camera lens and orbit stay finite");
    }
    void Settle(CameraController& camera, const PlayerController& player, int frames = 300) {
        for (int frame = 0; frame < frames; ++frame) camera.Update(kDt, CameraInput{}, player.State());
        FiniteCamera(camera);
    }
    PortalCameraRequest PortalRequest() {
        PortalCameraRequest request;
        request.entrySurface = glm::mat4(1.0f);
        request.exitSurface = glm::translate(glm::mat4(1.0f), glm::vec3(20.0f, 0.0f, 0.0f));
        request.portalMap = request.exitSurface * glm::rotate(glm::mat4(1.0f), glm::pi<float>(), glm::vec3(0, 1, 0));
        request.mappedFollowTarget = glm::vec3(20.0f, 0.0f, 1.0f);
        return request;
    }

    void TestPlayerLifeCycle() {
        PlayerController player;
        Require(player.CanControl() && player.State().isAlive, "new player can be controlled");
        player.UnlockJump(); player.UnlockHorn(); player.UnlockRadio();
        player.PublishMotion(42.0f, 0.6f, 0.1f, 0.2f, glm::vec3(1, 2, 3));
        const auto beforeDeath = player.State().motionResetRevision;
        Require(player.Die(), "gameplay death request is accepted once");
        Require(!player.CanControl() && player.State().deathCount == 1 &&
            player.State().motionResetRevision > beforeDeath, "death disables control and invalidates drive history");
        player.UpdateEffects(0.08f);
        const auto deathRevision = player.State().motionResetRevision;
        Require(!player.Die(), "duplicate death is rejected");
        Near(player.State().deathTimer, 0.08f, 0.0001f, "duplicate death preserves elapsed effects");
        Require(player.State().deathCount == 1 && player.State().motionResetRevision == deathRevision,
            "duplicate death preserves count and motion revision");
        player.SetControlEnabled(true);
        player.PublishMotion(50.0f, 0.8f, 0.0f, 0.0f);
        Require(!player.CanControl() && !player.State().isAlive, "control or telemetry updates cannot revive player");
        Require(player.Respawn(1.2f, glm::vec3(7, 8, 9)), "only explicit respawn restores player");
        Require(player.CanControl() && !player.State().isExtremeSpeed && player.State().deathCount == 1,
            "respawn restores control and clears velocity without erasing death count");
        Require(player.State().jumpEnabled && player.State().hornEnabled && player.State().radioEnabled,
            "respawn retains collected abilities");
        SameVector(player.State().position, glm::vec3(7, 8, 9), "respawn publishes checkpoint position");
        Near(player.State().bikeYaw, 1.2f, 0.0001f, "respawn publishes checkpoint heading");
        const auto respawnRevision = player.State().motionResetRevision;
        Require(!player.Respawn(2.0f) && player.State().motionResetRevision == respawnRevision,
            "duplicate respawn cannot reset a live player");
        player.ResetForNewRun();
        Require(player.CanControl() && player.State().deathCount == 0 && !player.State().jumpEnabled &&
            !player.State().hornEnabled && !player.State().radioEnabled &&
            player.State().motionResetRevision > respawnRevision, "new run resets gameplay and invalidates old drive input");
        std::puts("PASS player death/respawn idempotence, abilities, checkpoints and new-run reset");
    }

    void TestPlayerEffectsAndMotion() {
        PlayerController player;
        player.SetControlEnabled(false);
        const auto disabledRevision = player.State().motionResetRevision;
        player.SetControlEnabled(false);
        Require(player.State().motionResetRevision == disabledRevision, "repeated control suppression does not reset every frame");
        player.PublishMotion(-36.0f, 0.4f, 0.2f, 0.3f, glm::vec3(4, 5, 6));
        Require(!player.CanControl() && player.State().isExtremeSpeed, "disabled control still accepts extreme-speed telemetry");
        player.NotifyTeleported(1.1f, glm::vec3(9, 10, 11));
        Require(player.State().motionResetRevision > disabledRevision && !player.CanControl(),
            "teleport invalidates pedal/steering history without re-enabling control");
        Near(player.State().bikeSpeed, 36.0f, 0.0001f, "teleport preserves speed");
        Near(player.State().bikeSteerAngle, 0, 0.0001f, "teleport clears steering history");
        SameVector(player.State().position, glm::vec3(9, 10, 11), "teleport publishes new position");
        player.PublishMotion(35.99f, 0, 0, 0);
        Require(!player.State().isExtremeSpeed, "dropping below the threshold clears extreme speed");
        SameVector(player.State().position, glm::vec3(9, 10, 11), "motion-only feedback preserves position");
        player.Die();
        player.UpdateEffects(0.16f);
        Near(player.State().deathFactor, 1, 0.0001f, "death effect reaches its peak");
        for (const float dt : {0.0f, -1.0f, std::numeric_limits<float>::quiet_NaN()}) player.UpdateEffects(dt);
        Near(player.State().deathTimer, 0.16f, 0.0001f, "inactive or invalid simulation time does not advance effects");
        player.UpdateEffects(4.0f);
        Require(player.State().deathFactor > 0 && player.State().deathFactor < 1, "death effect decays over simulation time");
        player.UpdateEffects(10.0f);
        Near(player.State().deathFactor, 0, 0.0001f, "death effect finishes");
        Require(!player.State().isAlive, "finishing a visual effect never revives the player");
        std::puts("PASS player motion feedback, control suppression, teleport and simulation-owned death effects");
    }

    void TestCameraCannotControlPlayerLife() {
        PlayerController player;
        CameraController camera;
        camera.SetOrbit(0.2f, 0.3f, 6.0f, true);
        for (int pass = 0; pass < 2; ++pass) {
            if (pass == 1) player.Die();
            const bool alive = player.State().isAlive;
            const auto revision = player.State().motionResetRevision;
            for (int repeat = 0; repeat < 4; ++repeat) {
                camera.RequestFree(); camera.Update(kDt, CameraInput{}, player.State());
                Require(camera.State().mode == CameraMode::Free, "request switches to Free");
                camera.RequestFollow(); camera.Update(kDt, CameraInput{}, player.State());
                Require(camera.State().mode == CameraMode::Follow, "request switches to Follow");
                Require(player.State().isAlive == alive && player.CanControl() == alive &&
                    player.State().motionResetRevision == revision, "camera mode never revives or suppresses player control");
            }
        }
        std::puts("PASS camera switching cannot revive the player or disable riding permission");
    }

    void TestExtremeSpeedAcrossCameraModes() {
        PlayerController player;
        CameraController camera;
        camera.SetOrbit(0.0f, 0.25f, 6.0f, true);
        for (const auto mode : {CameraMode::Follow, CameraMode::Free, CameraMode::Portal, CameraMode::Cinematic}) {
            camera.EndCinematic(); camera.CancelPortal();
            if (mode == CameraMode::Follow) camera.RequestFollow();
            if (mode == CameraMode::Free) camera.RequestFree();
            if (mode == CameraMode::Portal) Require(camera.BeginPortal(PortalRequest()), "portal accepts takeover");
            if (mode == CameraMode::Cinematic) Require(camera.BeginCinematic(), "cinematic accepts takeover");
            for (const float speed : {36.0f, 12.0f, -40.0f, 0.0f}) {
                player.PublishMotion(speed, 0, 0, 0);
                Require(player.State().isExtremeSpeed == (std::abs(speed) >= 36.0f),
                    "physics telemetry updates extreme speed before camera runs");
                camera.Update(kDt, CameraInput{}, player.State());
                Require(player.State().isExtremeSpeed == (std::abs(speed) >= 36.0f),
                    "any active camera leaves player telemetry authoritative");
                FiniteCamera(camera);
            }
        }
        std::puts("PASS extreme speed enters and exits independently of every camera mode");
    }

    void TestManualLensAndOrbit() {
        PlayerController player;
        CameraController camera;
        camera.SetOrbit(0.1f, 0.2f, 4.0f, true);
        camera.SetFov(43.0f);
        camera.SetOrbit(0.8f, 0.5f, 18.0f);
        camera.Update(kDt, CameraInput{}, player.State());
        Require(camera.State().Distance > 4 && camera.State().Distance < 18,
            "follow distance converges rather than jumping to the new target");
        Settle(camera, player);
        Near(camera.State().Distance, 18, 0.01f, "manual distance converges");
        Near(camera.State().Yaw, 0.8f, 0.01f, "manual yaw converges");
        Near(camera.State().Pitch, 0.5f, 0.01f, "manual pitch converges");
        Near(camera.State().cameraFov, 43, 0.01f, "manual FOV survives automatic follow updates");
        Require(!camera.State().automaticFov, "manual lens disables automatic lens ownership");
        player.PublishMotion(45, 0, 0, 0.1f);
        Settle(camera, player);
        Near(camera.State().targetFov, 43, 0.01f, "extreme-speed presentation cannot overwrite the manual lens target");
        camera.RequestFree();
        const float beforeZoom = camera.State().cameraFov;
        CameraInput input; input.fovDelta = 12;
        camera.Update(kDt, input, player.State());
        Require(camera.State().cameraFov > beforeZoom, "Free mode applies lens input to actual FOV");
        Near(camera.State().targetFov, 55, 0.001f, "lens input changes the persistent target");
        Settle(camera, player);
        Near(camera.State().cameraFov, 55, 0.01f, "Free lens converges to the manual target");
        std::puts("PASS persistent manual lens/orbit targets and smooth Follow/Free camera updates");
    }

    void TestFreeMovementAndInactiveTime() {
        PlayerController player;
        CameraController camera;
        camera.RequestFree();
        const glm::mat4 original = glm::translate(glm::mat4(1), glm::vec3(3, 4, 5));
        Require(camera.SetFreeTransform(original), "set free-camera transform");
        CameraInput input; input.localMove = glm::vec3(0, 0, -6);
        input.allowFreeMovement = false;
        camera.Update(kDt, input, player.State());
        SameMatrix(camera.State().camera2world, original, "input suppression prevents free-camera translation");
        input.allowFreeMovement = true;
        camera.Update(kDt, input, player.State());
        SameVector(glm::vec3(camera.State().camera2world[3]), glm::vec3(3, 4, 4.9f),
            "free movement consumes a velocity and dt exactly once");
        const auto frozen = camera.State();
        input.lookDelta = glm::vec2(0.8f, 0.2f); input.fovDelta = 15;
        camera.Update(0, input, player.State());
        SameMatrix(camera.State().camera2world, frozen.camera2world, "zero dt does not rotate or move the camera");
        Near(camera.State().cameraFov, frozen.cameraFov, 0.0001f, "zero dt does not advance lens smoothing");
        FiniteCamera(camera);
        std::puts("PASS free movement units, explicit input suppression and zero-time stability");
    }

    void TestCinematicPriorityAndRecovery() {
        PlayerController player;
        for (const bool startFree : {false, true}) {
            CameraController camera;
            camera.SetOrbit(0.3f, 0.4f, 7, true);
            camera.SetFov(51);
            Settle(camera, player);
            if (startFree) camera.RequestFree();
            const auto before = camera.State();
            Require(camera.BeginCinematic(), "cinematic starts");
            const glm::mat4 shot = glm::translate(glm::mat4(1), glm::vec3(20, 30, 40));
            Require(camera.SetCinematicTransform(shot), "cinematic director supplies the active pose");
            Require(!camera.RequestFollow() && !camera.RequestFree() && !camera.BeginPortal(PortalRequest()),
                "cinematic rejects lower-priority mode and portal requests");
            CameraInput input; input.lookDelta = glm::vec2(1); input.localMove = glm::vec3(10); input.fovDelta = 20;
            camera.Update(kDt, input, player.State());
            SameMatrix(camera.State().camera2world, shot, "normal input cannot overwrite a cinematic shot");
            camera.EndCinematic();
            Require(camera.State().mode == before.mode && !camera.State().portalCameraActive,
                "cinematic ends at the original basic mode");
            Near(camera.State().targetYaw, before.targetYaw, 0.0001f, "cinematic restores orbit yaw target");
            Near(camera.State().targetPitch, before.targetPitch, 0.0001f, "cinematic restores orbit pitch target");
            Near(camera.State().targetDistance, before.targetDistance, 0.0001f, "cinematic restores distance target");
            Near(camera.State().targetFov, before.targetFov, 0.0001f, "cinematic restores manual lens target");
            const auto restored = camera.State().camera2world;
            camera.EndCinematic();
            SameMatrix(camera.State().camera2world, restored, "ending an already-ended cinematic is harmless");
        }
        std::puts("PASS cinematic priority, input isolation and original Follow/Free target recovery");
    }

    void TestPortalCancellationAndPreemption() {
        PlayerController player;
        CameraController camera;
        camera.SetOrbit(0.5f, 0.3f, 9, true);
        camera.RequestFree();
        const auto original = camera.State();
        Require(camera.BeginPortal(PortalRequest()), "portal accepts Free-camera takeover");
        Require(camera.State().mode == CameraMode::Portal && camera.State().portalCameraActive, "portal owns the camera");
        Require(!camera.RequestFollow() && !camera.RequestFree(), "ordinary mode requests cannot interrupt portal traversal");
        camera.Update(kDt, CameraInput{}, player.State());
        FiniteCamera(camera);
        camera.CancelPortal();
        Require(camera.State().mode == CameraMode::Free && !camera.State().portalCameraActive, "cancel restores original Free mode");
        Require(camera.State().targetDistance > 0 && camera.State().targetDistance <= original.Distance,
            "portal cancellation retains a finite bounded traversal distance");
        Require(camera.BeginPortal(PortalRequest()), "portal can start again after cancellation");
        Require(camera.BeginCinematic(), "higher-priority cinematic preempts a portal");
        Require(!camera.State().portalCameraActive, "preempted portal is invalidated");
        camera.EndCinematic();
        Require(camera.State().mode == CameraMode::Free && !camera.State().portalCameraActive,
            "cinematic completion restores base mode without reviving a stale portal");
        camera.CancelPortal();
        Require(camera.State().mode == CameraMode::Free, "late portal cancellation leaves the restored mode intact");
        std::puts("PASS portal cancellation and cinematic preemption cannot restore an invalid portal");
    }

    void TestPortalHandoffAndImmediateTeleport() {
        for (const bool startFree : {false, true}) {
            PlayerController player;
            CameraController camera;
            camera.SetOrbit(0, 0, 5, true);
            camera.SetFov(52);
            camera.Update(kDt, CameraInput{}, player.State());
            if (startFree) camera.RequestFree();
            auto request = PortalRequest();
            request.mappedFollowTarget = glm::vec3(20, 0, 0);
            Require(camera.BeginPortal(request), "begin traversal before the camera crosses the entry plane");
            player.PublishMotion(0, 0, 0, 0, glm::vec3(20, 0, 10));
            int frames = 0;
            while (camera.State().portalCameraActive && frames < 30) {
                camera.Update(kDt, CameraInput{}, player.State());
                FiniteCamera(camera);
                ++frames;
            }
            Require(frames >= 6 && frames < 30 && !camera.State().portalCameraActive,
                "camera crossing completes portal handoff after its minimum hold time");
            Require(camera.State().mode == (startFree ? CameraMode::Free : CameraMode::Follow),
                "natural portal completion restores the original basic mode");
            Near(camera.State().targetFov, 52, 0.0001f, "portal handoff retains the manual lens preference");
            Near(camera.State().portalCameraTimer, 0, 0.0001f, "finished portal clears its timer");
            Require(camera.State().Distance > 0 && camera.State().Distance <= 5.001f,
                "handoff preserves a bounded, nonzero camera boom");
            request.teleportImmediately = true;
            Require(camera.BeginPortal(request), "immediate teleport accepts the mapped camera pose");
            Require(!camera.State().portalCameraActive &&
                camera.State().mode == (startFree ? CameraMode::Free : CameraMode::Follow),
                "instant teleport does not force Follow or leave a portal takeover active");
            FiniteCamera(camera);
        }
        std::puts("PASS natural portal handoff and immediate teleport restore Follow/Free without stale takeover state");
    }

    void TestGroupedStateAndViews() {
        UserState state;
        state.render.bloomStrength = 0.75f;
        state.render.iblEnabled = false;
        state.editor.renderMode = 7;
        state.editor.showCameraPanel = true;
        state.editor.activeParticleIndex = 4;
        state.editor.isSceneViewportHovered = true;
        state.preferences.showHints = false;
        state.capabilities.wireframeSupported = true;
        state.renderStats.frustumCullingTotalCandidates = 42;
        state.renderOverrides.iblEnabled = true;
        Require(state.renderOverrides.IblEnabled(state.render) && !state.render.iblEnabled,
            "level lighting override does not overwrite the user's render preference");
        state.player.Die();
        state.camera.RequestFree();
        state.camera.SetFov(45);
        Require(state.gameFlow.Request(GameFlowCommand::Start) && state.gameFlow.Request(GameFlowCommand::Restart) &&
            state.gameFlow.TakeReloadRequest(), "session reset occurs inside the real reload protocol");
        const auto loadingRevision = state.gameFlow.Revision();
        state.ResetSession();
        Require(state.gameFlow.State() == GameFlowState::Loading && state.gameFlow.Revision() == loadingRevision &&
            !state.gameFlow.TakeReloadRequest(), "session reset preserves the consumed flow request");
        Require(state.gameFlow.CompleteReload(true) && state.gameFlow.CanSimulate(), "host can complete after session reset");
        Require(state.player.CanControl() && state.player.State().deathCount == 0 &&
            state.camera.State().mode == CameraMode::Follow && state.camera.State().automaticFov,
            "session reset rebuilds player and camera for a new run");
        Require(!state.renderOverrides.IblEnabled(state.render), "clearing a level override reveals the retained user preference");
        Near(state.render.bloomStrength, 0.75f, 0.0001f, "resetting game controllers preserves rendering settings");
        Require(state.editor.renderMode == 7 && state.editor.showCameraPanel && !state.preferences.showHints,
            "player/camera reset leaves editor configuration and gameplay preference intact");
        Require(state.capabilities.wireframeSupported && state.renderStats.frustumCullingTotalCandidates == 0 &&
            state.editor.activeParticleIndex == -1 && !state.editor.isSceneViewportHovered,
            "reset retains device capabilities but clears measurements and stale scene selection");
        auto ui = state.RuntimeUi();
        Require(&ui.player == &state.player && &ui.preferences == &state.preferences,
            "narrow views reference the composition-root instances");
        std::puts("PASS grouped state separates render preferences, level overrides and editor/game ownership");
    }
}

int main() {
    TestPlayerLifeCycle();
    TestPlayerEffectsAndMotion();
    TestCameraCannotControlPlayerLife();
    TestExtremeSpeedAcrossCameraModes();
    TestManualLensAndOrbit();
    TestFreeMovementAndInactiveTime();
    TestCinematicPriorityAndRecovery();
    TestPortalCancellationAndPreemption();
    TestPortalHandoffAndImmediateTeleport();
    TestGroupedStateAndViews();
    std::puts("PASS all real Player/Camera controller and state-boundary tests (CPU only)");
}
