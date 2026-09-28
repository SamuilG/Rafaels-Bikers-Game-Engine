#pragma once

#include "../Renderer/RenderUtilities/SceneEffects.hpp"
#include "../UserState/FrameExecution.hpp"
#include "../UserState/LevelState.hpp"
#include "../UserState/PlayerState.hpp"
#include <algorithm>
#include <cmath>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace engine {

// Demo-specific interpretation of vehicle state. Updated by the simulation
// owner, independently of swapchain acquisition or whether a frame is drawn.
class VehiclePresentation final {
public:
    const SceneEffects& State() const { return mState; }

    void ResetForNewRun() { mState = SceneEffects{}; }

    void Update(float dt, const PlayerState& player, const LevelState& level,
        const FrameExecution& frame, std::uint64_t entityId, std::uint32_t bodyId) {
        // A completed reload gets its own reset and no partial old-world frame.
        if (frame.reloadHandled) return;

        mState.subjectAlive = player.isAlive;
        mState.deathFactor = player.deathFactor;
        mState.portalTransition = BuildPortalTransition(level, entityId, bodyId);

        if (!frame.runSimulationSystems || !std::isfinite(dt) || dt <= 0.0f) return;
        const float speed = std::isfinite(player.bikeSpeed) ? std::abs(player.bikeSpeed) : 0.0f;
        const float target = std::clamp((speed - 20.0f) / 20.0f, 0.0f, 1.0f);
        // Preserve the previous 5*dt response, while keeping long frames bounded.
        mState.speedFactor += (target - mState.speedFactor) * std::min(5.0f * dt, 1.0f);
        mState.portalEffectTime += dt;
    }

private:
    static glm::vec3 NormalizeOr(const glm::vec3& value, const glm::vec3& fallback) {
        const float lengthSquared = glm::dot(value, value);
        return lengthSquared < 0.0001f ? fallback : value / std::sqrt(lengthSquared);
    }

    static glm::mat4 RigidFrame(const glm::mat4& surface) {
        glm::mat4 frame(1.0f);
        frame[0] = glm::vec4(NormalizeOr(glm::vec3(surface[0]), glm::vec3(1, 0, 0)), 0);
        frame[1] = glm::vec4(NormalizeOr(glm::vec3(surface[1]), glm::vec3(0, 1, 0)), 0);
        frame[2] = glm::vec4(NormalizeOr(glm::vec3(surface[2]), glm::vec3(0, 0, 1)), 0);
        frame[3] = glm::vec4(glm::vec3(surface[3]), 1);
        return frame;
    }

    static glm::vec4 FrontClipPlane(const glm::mat4& frame) {
        const glm::vec3 normal = NormalizeOr(glm::vec3(frame[2]), glm::vec3(0, 0, 1));
        const glm::vec3 point = glm::vec3(frame[3]) + normal * 0.025f;
        return glm::vec4(normal, -glm::dot(normal, point));
    }

    static PortalTransitionVisual BuildPortalTransition(const LevelState& level,
        std::uint64_t entityId, std::uint32_t bodyId) {
        PortalTransitionVisual result;
        if (!level.portalTransitionVisualActive) return result;

        result.active = true;
        result.entityId = entityId;
        result.bodyId = bodyId;
        const glm::mat4 entry = RigidFrame(level.portalTransitionEntrySurface);
        const glm::mat4 exit = RigidFrame(level.portalTransitionExitSurface);
        const glm::mat4 halfTurn = glm::rotate(glm::mat4(1.0f), glm::pi<float>(), glm::vec3(0, 1, 0));
        const glm::vec4 entryClip = FrontClipPlane(entry);
        const glm::vec4 exitClip = FrontClipPlane(exit);
        if (level.portalTransitionRealAtExit) {
            result.realClipPlane = exitClip;
            result.cloneClipPlane = entryClip;
            result.cloneMap = entry * halfTurn * glm::inverse(exit) *
                glm::translate(glm::mat4(1.0f), -level.portalTransitionExitCorrection);
        }
        else {
            result.realClipPlane = entryClip;
            result.cloneClipPlane = exitClip;
            result.cloneMap = exit * halfTurn * glm::inverse(entry);
        }
        return result;
    }

    SceneEffects mState;
};

} // namespace engine
