#include "trigger.hpp"

#include "../Debug/DebugRenderer.hpp"
#include "../Particle/ParticleSystem.hpp"
#include"../UI/EngineUi.hpp"

#include <algorithm>
#include <cstdio>
#include <limits>

namespace engine {

    // box trigger
    size_t TriggerSystem::AddBoxTrigger(const glm::vec3& center, const glm::vec3& extents, size_t particleIndex, const glm::vec3& color, const glm::mat4& transform, bool isVisible, bool oneShot) {
        TriggerVolume trigger{};
        trigger.shape = TriggerShape::Box;
        trigger.center = center;
        trigger.extents = extents;
        trigger.color = color;
        trigger.transform = transform;
        trigger.particleIndex = particleIndex;
        trigger.isVisible = isVisible;
        trigger.oneShot = oneShot;
        mTriggers.push_back(trigger);
        return mTriggers.size() - 1;
    }

    // sphere trigger
    size_t TriggerSystem::AddSphereTrigger(const glm::vec3& center, float radius, size_t particleIndex, const glm::vec3& color, bool isVisible, bool oneShot, int segments) {
        TriggerVolume trigger{};
        trigger.shape = TriggerShape::Sphere;
        trigger.center = center;
        trigger.radius = radius;
        trigger.color = color;
        trigger.particleIndex = particleIndex;
        trigger.isVisible = isVisible;
        trigger.oneShot = oneShot;
        trigger.segments = segments;
        mTriggers.push_back(trigger);
        return mTriggers.size() - 1;
    }

    // capsule trigger
    size_t TriggerSystem::AddCapsuleTrigger(const glm::vec3& center, float radius, float halfHeight, size_t particleIndex, const glm::vec3& color, bool isVisible, bool oneShot, int segments) {
        TriggerVolume trigger{};
        trigger.shape = TriggerShape::Capsule;
        trigger.center = center;
        trigger.radius = radius;
        trigger.halfHeight = halfHeight;
        trigger.color = color;
        trigger.particleIndex = particleIndex;
        trigger.isVisible = isVisible;
        trigger.oneShot = oneShot;
        trigger.segments = segments;
        mTriggers.push_back(trigger);
        return mTriggers.size() - 1;
    }

    // hide or show the debug wireframe
    void TriggerSystem::SetTriggerVisible(size_t triggerId, bool isVisible) {
        if (triggerId >= mTriggers.size()) return;
        mTriggers[triggerId].isVisible = isVisible;
    }

    
    void TriggerSystem::SetTriggerEnabled(size_t triggerId, bool isEnabled) {
        if (triggerId >= mTriggers.size()) return;
        mTriggers[triggerId].isEnabled = isEnabled;
    }

    
    void TriggerSystem::SetTriggerCallbacks(size_t triggerId, std::function<void()> onEnter, std::function<void()> onExit) {
        if (triggerId >= mTriggers.size()) return;
        mTriggers[triggerId].onEnter = std::move(onEnter);
        mTriggers[triggerId].onExit = std::move(onExit);
    }

    
    void TriggerSystem::ClearTriggers() {
        mTriggers.clear();
    }

    
    void TriggerSystem::ProcessParticleTriggers(const glm::vec3& probePosition, std::vector<std::unique_ptr<ParticleSystem>>& particles) {
        if (particles.empty()) return;

        std::vector<bool> bound(particles.size(), false);
        std::vector<bool> active(particles.size(), false);

        for (auto& ps : particles) {
            ps->config.triggerControlled = false;
        }

        for (auto& trigger : mTriggers) {
            if (!trigger.isEnabled || trigger.particleIndex >= particles.size()) continue;

            bool inside = false;

            switch (trigger.shape) {
            case TriggerShape::Box: {
                glm::vec4 transformed = glm::inverse(trigger.transform) * glm::vec4(probePosition, 1.0f);
                glm::vec3 localPoint = glm::vec3(transformed);
                glm::vec3 minBound = trigger.center - trigger.extents;
                glm::vec3 maxBound = trigger.center + trigger.extents;
                inside =
                    localPoint.x >= minBound.x && localPoint.x <= maxBound.x &&
                    localPoint.y >= minBound.y && localPoint.y <= maxBound.y &&
                    localPoint.z >= minBound.z && localPoint.z <= maxBound.z;
                break;
            }
            case TriggerShape::Sphere:
                inside = glm::distance(probePosition, trigger.center) <= trigger.radius;
                break;
            case TriggerShape::Capsule: {
                glm::vec3 topCenter = trigger.center + glm::vec3(0.0f, trigger.halfHeight, 0.0f);
                glm::vec3 bottomCenter = trigger.center - glm::vec3(0.0f, trigger.halfHeight, 0.0f);
                glm::vec3 segment = topCenter - bottomCenter;
                float segmentLengthSq = glm::dot(segment, segment);
                float t = 0.0f;
                if (segmentLengthSq > std::numeric_limits<float>::epsilon()) {
                    t = glm::clamp(glm::dot(probePosition - bottomCenter, segment) / segmentLengthSq, 0.0f, 1.0f);
                }
                glm::vec3 closestPoint = bottomCenter + segment * t;
                inside = glm::distance(probePosition, closestPoint) <= trigger.radius;
                break;
            }
            }

            bound[trigger.particleIndex] = true;

            // trigger: fire enter and exit callbacks only on boundary crossings
            if (inside && !trigger.wasInside && trigger.onEnter) {
                trigger.onEnter();
            }
            if (!inside && trigger.wasInside && trigger.onExit) {
                trigger.onExit();
            }
            trigger.wasInside = inside;

            if (trigger.oneShot) {
                if (inside) {
                    trigger.hasTriggered = true;
                }
                if (trigger.hasTriggered) {
                    active[trigger.particleIndex] = true;
                }
            }
            else if (inside) {
                active[trigger.particleIndex] = true;
            }
        }

        for (size_t i = 0; i < particles.size(); ++i) {
            if (!bound[i]) continue;
            particles[i]->config.triggerControlled = true;
            particles[i]->config.isVisible = active[i];
        }
    }

    // show debug draw box
    void TriggerSystem::DrawTriggers(DebugRenderer& debugRenderer) const {
        for (const auto& trigger : mTriggers) {
            if (!trigger.isVisible) continue;

            switch (trigger.shape) {
            case TriggerShape::Box:
                debugRenderer.DrawBox(trigger.center, trigger.extents, trigger.color, trigger.transform);
                break;
            case TriggerShape::Sphere:
                debugRenderer.DrawSphere(trigger.center, trigger.radius, trigger.color, trigger.segments);
                break;
            case TriggerShape::Capsule:
                debugRenderer.DrawCapsule(trigger.center, trigger.radius, trigger.halfHeight, trigger.color, trigger.segments);
                break;
            }
        }
    }

} // namespace engine
