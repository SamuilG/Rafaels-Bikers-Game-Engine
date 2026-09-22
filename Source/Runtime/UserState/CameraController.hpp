#pragma once
#include "CameraState.hpp"
#include "PlayerState.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/constants.hpp>
#include <algorithm>
#include <cmath>

namespace engine {
class CameraController {
public:
    const CameraState& State() const { return mState; }
    void ResetForNewRun() { *this = CameraController{}; }
    bool RequestFollow() { return RequestMode(CameraMode::Follow); }
    bool RequestFree() { return RequestMode(CameraMode::Free); }
    void SetFollowTarget(const glm::vec3& target) { mState.followTargetPos = target; }
    void HoldAutoAlign() { mState.cameraIdleTimer = 0.0f; }
    void SetOrbit(float yaw, float pitch, float distance, bool snap = false) {
        if (IsTakenOver()) return;
        mState.targetYaw = yaw;
        mState.targetPitch = std::clamp(pitch, glm::radians(-85.0f), glm::radians(85.0f));
        mState.targetDistance = std::clamp(distance, 2.0f, 70.0f);
        HoldAutoAlign();
        if (snap) {
            mState.Yaw = mState.targetYaw;
            mState.Pitch = mState.targetPitch;
            mState.Distance = mState.targetDistance;
        }
    }
    void SetFov(float degrees) {
        if (mState.mode == CameraMode::Cinematic || !std::isfinite(degrees)) return;
        mState.automaticFov = false;
        mState.targetFov = std::clamp(degrees, 10.0f, 120.0f);
    }
    void UseAutomaticFov(bool enabled) {
        if (mState.mode != CameraMode::Cinematic) mState.automaticFov = enabled;
    }

    void SetOcclusionFadeEnabled(bool enabled) {
        mState.occlusionFadeEnabled = enabled;
    }

    void SetOcclusionFadeCoverage(float coverage) {
        mState.occlusionFadeCoverage = std::clamp(coverage, 0.0f, 1.0f);
    }

    void SetOcclusionFadeSpeed(float speed) {
        mState.occlusionFadeSpeed = std::clamp(speed, 0.1f, 30.0f);
    }

    void SetOcclusionTargetHeight(float height) {
        mState.occlusionTargetHeight = std::clamp(height, 0.0f, 3.0f);
    }
    bool SetFreeTransform(const glm::mat4& transform) {
        if (mState.mode != CameraMode::Free) return false;
        mState.camera2world = transform;
        return true;
    }
    bool BeginCinematic() {
        if (mState.mode == CameraMode::Cinematic) return true;
        CancelPortal();
        mCinematicReturn = mState;
        mState.mode = CameraMode::Cinematic;
        mState.thirdPersonMode = false;
        return true;
    }
    bool SetCinematicTransform(const glm::mat4& transform) {
        if (mState.mode != CameraMode::Cinematic) return false;
        mState.camera2world = transform;
        return true;
    }
    void EndCinematic() {
        if (mState.mode != CameraMode::Cinematic) return;
        mState = mCinematicReturn;
        mBaseMode = mState.mode;
    }
    void CancelPortal() {
        mState.portalCameraActive = false;
        mState.portalCameraTimer = 0.0f;
        mState.portalCameraBoomLength = 0.0f;
        mState.portalCameraStartSide = 1.0f;
        mState.portalCameraHandoffDistance = 0.20f;
        mState.portalCameraPosition = glm::vec3(0.0f);
        mState.portalCameraTargetPosition = glm::vec3(0.0f);
        mState.portalCameraBoomOffset = glm::vec3(0.0f);
        mState.portalCameraEntrySurface = glm::mat4(1.0f);
        mState.portalCameraExitSurface = glm::mat4(1.0f);
        mState.portalCameraInverseExitSurface = glm::mat4(1.0f);
        if (mState.mode == CameraMode::Portal) {
            mState.mode = mBaseMode;
            mState.thirdPersonMode = mBaseMode == CameraMode::Follow;
        }
    }
    void ResetFollow() {
        const glm::vec3 target = mState.followTargetPos;
        const bool automaticFov = mState.automaticFov;
        const float fov = mState.targetFov;
        mState = CameraState{};
        mState.followTargetPos = target;
        mState.automaticFov = automaticFov;
        mState.cameraFov = mState.targetFov = fov;
        mState.camera2world = glm::inverse(glm::lookAt(target + glm::vec3(0.0f,1.6f,5.0f),
            target + glm::vec3(0.0f,1.6f,0.0f), glm::vec3(0.0f,1.0f,0.0f)));
        mBaseMode = CameraMode::Follow;
    }
    bool ApplyTeleportPose(const glm::mat4& pose, const glm::vec3& target, float distance) {
        if (mState.mode == CameraMode::Cinematic) return false;
        CancelPortal();
        mState.mode = mBaseMode;
        mState.thirdPersonMode = mBaseMode == CameraMode::Follow;
        mState.camera2world = pose;
        mState.followTargetPos = target;
        const glm::vec3 offset = normalize_or(glm::vec3(pose[3]) - target - glm::vec3(0,1.6f,0),
            glm::vec3(pose[2]));
        mState.Distance = mState.targetDistance = std::max(distance, 0.1f);
        mState.Yaw = mState.targetYaw = std::atan2(offset.x, offset.z);
        mState.Pitch = mState.targetPitch = std::asin(std::clamp(offset.y, -1.0f, 1.0f));
        HoldAutoAlign();
        return true;
    }
    bool BeginPortal(const PortalCameraRequest& request) {
        if (mState.mode == CameraMode::Cinematic) return false;
        const float cap = std::max(0.1f, mState.Distance > 0.1f ? mState.Distance : mState.targetDistance);
        if (request.teleportImmediately) {
            const float preservedDistance = std::max(mState.Distance, 0.1f);
            const float preservedTargetDistance = std::max(mState.targetDistance, 0.1f);
            glm::mat4 mapped = request.portalMap * mState.camera2world;
            mapped[3] += glm::vec4(request.exitCorrection, 0.0f);
            const glm::vec3 target = request.mappedFollowTarget + glm::vec3(0,1.6f,0);
            const glm::vec3 offset = glm::vec3(mapped[3]) - target;
            mapped[3] = glm::vec4(
                target + normalize_or(offset, glm::vec3(mapped[2])) * preservedDistance,
                1.0f);
            const bool applied = ApplyTeleportPose(mapped, request.mappedFollowTarget, preservedDistance);
            if (applied) {
                // Extreme-speed traversal is a rigid-space transform. Preserve
                // both sides of the distance interpolator so the portal itself
                // cannot introduce a zoom pulse or restart the chase-camera blend.
                mState.Distance = preservedDistance;
                mState.targetDistance = preservedTargetDistance;
            }
            return applied;
        }
        CancelPortal();
        mState.mode = CameraMode::Portal;
        mState.thirdPersonMode = true;
        mState.portalCameraActive = true;
        mState.followTargetPos = request.mappedFollowTarget;
        mState.portalCameraEntrySurface = request.entrySurface;
        mState.portalCameraExitSurface = request.exitSurface;
        mState.portalCameraInverseExitSurface = glm::inverse(request.exitSurface);
        mState.portalCameraHandoffDistance = std::max(request.cameraHandoffDistance, 0.0f);
        const glm::mat4 exitToEntry = request.entrySurface * portal_half_turn() * glm::inverse(request.exitSurface);
        mState.portalCameraTargetPosition = transform_point(exitToEntry, request.mappedFollowTarget + glm::vec3(0,1.6f,0));
        const glm::vec3 offset = glm::vec3(mState.camera2world[3]) - mState.portalCameraTargetPosition;
        const float distance = std::clamp(glm::length(offset), 0.1f, cap);
        const glm::vec3 direction = normalize_or(offset, glm::vec3(mState.camera2world[2]));
        mState.portalCameraBoomOffset = direction * distance;
        mState.portalCameraPosition = mState.portalCameraTargetPosition + mState.portalCameraBoomOffset;
        mState.portalCameraBoomLength = distance;
        mState.Distance = mState.targetDistance = distance;
        mState.Yaw = mState.targetYaw = std::atan2(direction.x, direction.z);
        mState.Pitch = mState.targetPitch = std::asin(std::clamp(direction.y, -1.0f, 1.0f));
        const float side = transform_point(glm::inverse(request.entrySurface), mState.portalCameraPosition).z;
        mState.portalCameraStartSide = side < 0.0f ? -1.0f : 1.0f;
        HoldAutoAlign();
        return true;
    }
    void Update(float dt, const CameraInput& input, const PlayerState& player) {
        if (mState.mode == CameraMode::Cinematic || !std::isfinite(dt) || dt <= 0.0f) return;
        const float aElapsedTime = std::min(dt, 0.05f);
        const float dx = input.lookDelta.x, dy = input.lookDelta.y;
        if (mState.thirdPersonMode) {
            glm::vec3 right(std::cos(mState.Yaw), 0.0f, -std::sin(mState.Yaw));
            if (mState.portalCameraActive) {
                const glm::mat4 entryToExit = mState.portalCameraExitSurface * portal_half_turn() *
                    glm::inverse(mState.portalCameraEntrySurface);
                right = transform_vector(entryToExit, right);
                right.y = 0.0f;
                right = normalize_or(right, glm::vec3(std::cos(mState.Yaw), 0.0f, -std::sin(mState.Yaw)));
            }
            mState.followTargetPos = player.position + right + glm::vec3(0.0f, -1.0f, 0.0f);
        }
        auto& cam = mState.camera2world;
        if (input.fovDelta != 0.0f) SetFov(mState.targetFov + input.fovDelta);
	if (mState.thirdPersonMode)
	{
		if (player.isExtremeSpeed) {
			float transitionSpeed = 7.5f;
			mState.targetDistance += (0.5f - mState.targetDistance) * transitionSpeed * aElapsedTime;
			if (mState.automaticFov) mState.targetFov += (120.0f - mState.targetFov) * transitionSpeed * aElapsedTime;
		}
		else {
			if (mState.targetDistance < 2.0f) {
				float recoverySpeed = 4.0f;
				mState.targetDistance += (2.0f - mState.targetDistance) * recoverySpeed * aElapsedTime;
			}
			if (input.distanceDelta != 0.0f) {
				float scroll = input.distanceDelta;
				if (scroll != 0.0f) {
					mState.targetDistance += scroll;
					mState.targetDistance = std::clamp(mState.targetDistance, 2.0f, 70.0f);
				}
			}
			constexpr float kPortalAutoPullActivationDistance = 8.0f;
			if (mState.portalCameraActive && mState.Distance > kPortalAutoPullActivationDistance) {
				constexpr float kPortalAutoPullPerMeter = 5.25f;
				const glm::vec3 portalRealTarget = mState.followTargetPos + glm::vec3(0.0f, 1.6f, 0.0f);
				const glm::vec3 exitLocalTarget = transform_point(
					glm::inverse(mState.portalCameraExitSurface),
					portalRealTarget);
				const float exitTravel = std::max(0.0f, exitLocalTarget.z - 0.04f);
				const float pullStartDistance = std::max(mState.portalCameraBoomLength, mState.targetDistance);
				const float pulledDistance = std::clamp(
					pullStartDistance - exitTravel * kPortalAutoPullPerMeter,
					kPortalAutoPullActivationDistance,
					70.0f);
				mState.targetDistance = std::min(mState.targetDistance, pulledDistance);
			}
			float safeDist = std::max(mState.targetDistance, 2.0f);
			float distRatio = (safeDist - 2.0f) / (70.0f - 2.0f);
			if (mState.automaticFov) mState.targetFov = 100.0f - distRatio * (100.0f - 20.0f);
		}
		mState.targetFov = std::clamp(mState.targetFov, 10.0f, 120.0f);
		bool hasCameraInput = (std::abs(dx) > 0.001f || std::abs(dy) > 0.001f);
        if (input.holdAutoAlign) mState.cameraIdleTimer = 0.0f;
		if (hasCameraInput)
		{
			mState.cameraIdleTimer = 0.0f;
			mState.targetYaw -= dx;
			mState.targetPitch += dy;
			float const max_pitch = glm::radians(85.0f);
			mState.targetPitch = glm::clamp(mState.targetPitch, -max_pitch, max_pitch);
		}
		else
		{
			mState.cameraIdleTimer += aElapsedTime;
		}
		float autoAlignDelay = 0.5f;
		float minAlignSpeed = 2.0f;
		if (!mState.portalCameraActive && !input.holdAutoAlign &&
			(mState.cameraIdleTimer > autoAlignDelay || player.isExtremeSpeed) &&
			std::abs(player.bikeSpeed) > minAlignSpeed) {
			float fullEffectDist = 2.0f;
			float noEffectDist = 20.0f;
			float distanceFactor = 1.0f - glm::clamp((mState.Distance - fullEffectDist) / (noEffectDist - fullEffectDist), 0.0f, 1.0f);
			if (distanceFactor > 0.001f) {
				float diff = player.bikeYaw - mState.targetYaw;
				while (diff > glm::pi<float>())  diff -= 2.0f * glm::pi<float>();
				while (diff < -glm::pi<float>()) diff += 2.0f * glm::pi<float>();
				float baseAlignSpeed = 3.0f;
				if (player.isExtremeSpeed) {
					baseAlignSpeed = 8.0f;
				}
				float alignSpeed = baseAlignSpeed * distanceFactor;
				mState.targetYaw += diff * alignSpeed * aElapsedTime;
				float defaultPitch = glm::radians(15.0f);
				mState.targetPitch += (defaultPitch - mState.targetPitch) * alignSpeed * aElapsedTime;
			}
		}
		float smoothness = 6.0f;
		mState.Yaw += (mState.targetYaw - mState.Yaw) * smoothness * aElapsedTime;
		mState.Pitch += (mState.targetPitch - mState.Pitch) * smoothness * aElapsedTime;
		mState.Distance += (mState.targetDistance - mState.Distance) * smoothness * aElapsedTime;
		float rollMultiplier = 0.6f;
		if (player.isExtremeSpeed) {
			mState.targetCameraRoll = player.bikeLeanAngle * rollMultiplier;
		}
		else {
			mState.targetCameraRoll = 0.0f;
		}
		mState.cameraRoll += (mState.targetCameraRoll - mState.cameraRoll) * smoothness * aElapsedTime;
		glm::vec3 char_pos = mState.followTargetPos;
		glm::vec3 eye_offset(0.f, 1.6f, 0.f);
		glm::vec3 target_pos = char_pos + eye_offset;
		glm::vec3 offset;
		offset.x = mState.Distance * std::cos(mState.Pitch) * std::sin(mState.Yaw);
		offset.y = mState.Distance * std::sin(mState.Pitch);
		offset.z = mState.Distance * std::cos(mState.Pitch) * std::cos(mState.Yaw);
		glm::vec3 cam_pos = target_pos + offset;
		if (mState.portalCameraActive) {
			mState.portalCameraTimer += aElapsedTime;
			const glm::vec3 realTargetPos = target_pos;
			const glm::mat4 exitToEntry = mState.portalCameraEntrySurface *
				portal_half_turn() *
				glm::inverse(mState.portalCameraExitSurface);
			const glm::vec3 perceivedTargetPos = transform_point(exitToEntry, realTargetPos);
			mState.portalCameraTargetPosition = perceivedTargetPos;
			const glm::vec3 requestedBoomOffset = offset;
			const float requestedBoomLength = std::max(0.1f, glm::length(requestedBoomOffset));
			const float storedBoomLength = glm::length(mState.portalCameraBoomOffset);
			glm::vec3 boomDir = normalize_or(mState.portalCameraBoomOffset, requestedBoomOffset);
			if (hasCameraInput) {
				boomDir = normalize_or(requestedBoomOffset, mState.portalCameraBoomOffset);
			}
			float lockedBoomLength = mState.portalCameraBoomLength > 0.1f ?
				mState.portalCameraBoomLength :
				std::max(0.1f, storedBoomLength > 0.1f ? storedBoomLength : requestedBoomLength);
			lockedBoomLength = std::min(lockedBoomLength, std::max(0.1f, mState.Distance));
			mState.portalCameraBoomLength = lockedBoomLength;
			mState.portalCameraBoomOffset = boomDir * lockedBoomLength;
			mState.Distance = lockedBoomLength;
			mState.targetDistance = std::min(mState.targetDistance, lockedBoomLength);
			const glm::vec3 portalSideCamPos = mState.portalCameraTargetPosition + mState.portalCameraBoomOffset;
			mState.portalCameraPosition = portalSideCamPos;
			target_pos = mState.portalCameraTargetPosition;
			cam_pos = mState.portalCameraPosition;
			const glm::mat4 entryInverse = glm::inverse(mState.portalCameraEntrySurface);
			const float cameraEntryLocalZ = glm::vec3(entryInverse * glm::vec4(cam_pos, 1.0f)).z;
			const float signedEntryDistance = mState.portalCameraStartSide * cameraEntryLocalZ;
			// Near-plane safety takes priority over presentation timing. Delaying the
			// handoff here leaves the portal quad between the camera and its near plane.
			if (signedEntryDistance <= mState.portalCameraHandoffDistance) {
				const glm::mat4 entryToExit = mState.portalCameraExitSurface *
					portal_half_turn() *
					entryInverse;
				glm::vec3 exitCamPos = transform_point(entryToExit, cam_pos);
				glm::vec3 exitOffset = exitCamPos - realTargetPos;
				const float rawExitDistance = glm::length(exitOffset);
				const glm::vec3 fallbackExitOffset = transform_vector(entryToExit, mState.portalCameraBoomOffset);
				exitOffset = normalize_or(exitOffset, fallbackExitOffset);
				const float currentBoomDistance = std::max(
					0.1f,
					std::min(
						mState.Distance,
						mState.portalCameraBoomLength > 0.1f ? mState.portalCameraBoomLength : mState.Distance));
				const float exitDistance = std::min(std::max(rawExitDistance, 0.1f), currentBoomDistance);
				exitCamPos = realTargetPos + exitOffset * exitDistance;
				mState.Distance = exitDistance;
				mState.targetDistance = exitDistance;
				mState.Yaw = std::atan2(exitOffset.x, exitOffset.z);
				mState.targetYaw = mState.Yaw;
				mState.Pitch = std::asin(glm::clamp(exitOffset.y, -1.0f, 1.0f));
				mState.targetPitch = mState.Pitch;
                CancelPortal();
				target_pos = realTargetPos;
				cam_pos = exitCamPos;
			}
		}
		glm::vec3 forwardDir = glm::normalize(target_pos - cam_pos);
		glm::vec3 globalUp(0.f, 1.f, 0.f);
		glm::mat4 rollTransform = glm::rotate(glm::mat4(1.0f), mState.cameraRoll, forwardDir);
		glm::vec3 dynamicUp = glm::vec3(rollTransform * glm::vec4(globalUp, 0.0f));
		glm::mat4 view_matrix = glm::lookAt(cam_pos, target_pos, dynamicUp);
		cam = glm::inverse(view_matrix);
	}
        else {
            if (dx != 0.0f || dy != 0.0f) {
                const glm::vec4 position = cam[3];
                cam[3] = glm::vec4(0,0,0,1);
                cam = glm::rotate(glm::mat4(1.0f), -dx, glm::vec3(0,1,0)) * cam;
                cam[3] = position;
                cam = glm::rotate(cam, -dy, glm::vec3(1,0,0));
            }
            if (input.allowFreeMovement) cam = cam * glm::translate(glm::mat4(1.0f), input.localMove * aElapsedTime);
        }
        mState.cameraFov += (mState.targetFov - mState.cameraFov) * 6.0f * aElapsedTime;
    }
private:
    bool IsTakenOver() const { return mState.mode == CameraMode::Portal || mState.mode == CameraMode::Cinematic; }
    bool RequestMode(CameraMode mode) {
        if (IsTakenOver()) return false;
        mBaseMode = mState.mode = mode;
        mState.thirdPersonMode = mode == CameraMode::Follow;
        HoldAutoAlign();
        return true;
    }
    static glm::mat4 portal_half_turn() {
        return glm::rotate(glm::mat4(1.0f), glm::pi<float>(), glm::vec3(0,1,0));
    }
    static glm::vec3 transform_point(const glm::mat4& matrix, const glm::vec3& point) {
        return glm::vec3(matrix * glm::vec4(point, 1.0f));
    }
    static glm::vec3 transform_vector(const glm::mat4& matrix, const glm::vec3& vector) {
        return glm::vec3(matrix * glm::vec4(vector, 0.0f));
    }
    static glm::vec3 normalize_or(const glm::vec3& value, const glm::vec3& fallback) {
        const float length = glm::dot(value, value);
        if (length > 0.0001f) return value / std::sqrt(length);
        const float fallbackLength = glm::dot(fallback, fallback);
        return fallbackLength > 0.0001f ? fallback / std::sqrt(fallbackLength) : glm::vec3(0,0,1);
    }
    CameraState mState;
    CameraState mCinematicReturn;
    CameraMode mBaseMode = CameraMode::Follow;
};
} // namespace engine
