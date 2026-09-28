#include "trigger.hpp"

#include <algorithm>
#include <limits>
#include <map>
#include <memory>
#include <utility>

namespace engine {
    namespace {
        constexpr size_t kUnbound = static_cast<size_t>(-1);

        std::function<void()> ShareCallback(std::function<void()> callback) {
            if (!callback) return {};
            // Update copies this wrapper for lifetime safety, while every call
            // still reaches the same callable and its mutable capture state.
            auto callable = std::make_shared<std::function<void()>>(std::move(callback));
            return [callable = std::move(callable)] { (*callable)(); };
        }

        bool Contains(const TriggerVolume& trigger, const glm::vec3& probePosition) {
            switch (trigger.shape) {
            case TriggerShape::Box: {
                const glm::vec3 point = glm::vec3(glm::inverse(trigger.transform) * glm::vec4(probePosition, 1.0f));
                const glm::vec3 minBound = trigger.center - trigger.extents;
                const glm::vec3 maxBound = trigger.center + trigger.extents;
                return point.x >= minBound.x && point.x <= maxBound.x &&
                    point.y >= minBound.y && point.y <= maxBound.y &&
                    point.z >= minBound.z && point.z <= maxBound.z;
            }
            case TriggerShape::Sphere:
                return glm::distance(probePosition, trigger.center) <= trigger.radius;
            case TriggerShape::Capsule: {
                const glm::vec3 bottom = trigger.center - glm::vec3(0.0f, trigger.halfHeight, 0.0f);
                const glm::vec3 segment(0.0f, 2.0f * trigger.halfHeight, 0.0f);
                const float lengthSquared = glm::dot(segment, segment);
                const float t = lengthSquared > std::numeric_limits<float>::epsilon()
                    ? glm::clamp(glm::dot(probePosition - bottom, segment) / lengthSquared, 0.0f, 1.0f)
                    : 0.0f;
                return glm::distance(probePosition, bottom + segment * t) <= trigger.radius;
            }
            }
            return false;
        }
    }

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
        mTriggers.push_back(std::move(trigger));
        return mTriggers.size() - 1;
    }

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
        mTriggers.push_back(std::move(trigger));
        return mTriggers.size() - 1;
    }

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
        mTriggers.push_back(std::move(trigger));
        return mTriggers.size() - 1;
    }

    void TriggerSystem::SetTriggerVisible(size_t triggerId, bool isVisible) {
        if (triggerId < mTriggers.size()) mTriggers[triggerId].isVisible = isVisible;
    }

    void TriggerSystem::SetTriggerEnabled(size_t triggerId, bool isEnabled) {
        if (triggerId >= mTriggers.size()) return;
        mTriggers[triggerId].isEnabled = isEnabled;
        if (!isEnabled) mTriggers[triggerId].wasInside = false;
    }

    void TriggerSystem::SetTriggerCallbacks(size_t triggerId, std::function<void()> onEnter, std::function<void()> onExit) {
        if (triggerId >= mTriggers.size()) return;
        mTriggers[triggerId].onEnter = ShareCallback(std::move(onEnter));
        mTriggers[triggerId].onExit = ShareCallback(std::move(onExit));
    }

    void TriggerSystem::ClearTriggers() {
        ++mGeneration;
        mTriggers.clear();
    }

    void TriggerSystem::OnParticleGroupRemoved(size_t particleIndex) {
        if (particleIndex == kUnbound) return;
        for (auto& trigger : mTriggers) {
            if (trigger.particleIndex == particleIndex) trigger.particleIndex = kUnbound;
            else if (trigger.particleIndex != kUnbound && trigger.particleIndex > particleIndex) --trigger.particleIndex;
        }
    }

    bool TriggerSystem::HasParticleBinding(size_t particleIndex) const {
        return particleIndex != kUnbound && std::any_of(mTriggers.begin(), mTriggers.end(), [particleIndex](const TriggerVolume& trigger) {
            return trigger.particleIndex == particleIndex;
        });
    }

    void TriggerSystem::Update(const glm::vec3& probePosition) {
        if (mUpdating) return;
        struct UpdateGuard {
            bool& updating;
            explicit UpdateGuard(bool& value) : updating(value) { updating = true; }
            ~UpdateGuard() { updating = false; }
        } guard(mUpdating);
        const size_t generation = mGeneration;
        const size_t count = mTriggers.size();
        for (size_t index = 0; index < count; ++index) {
            auto& trigger = mTriggers[index];
            if (!trigger.isEnabled || (trigger.oneShot && trigger.hasTriggered)) continue;
            const bool inside = Contains(trigger, probePosition);
            const bool entering = inside && !trigger.wasInside;
            const bool exiting = !inside && trigger.wasInside;
            // Commit state before invoking arbitrary callbacks. Copy the shared
            // wrapper so clearing/adding/replacing cannot destroy the callable,
            // and mutable value captures persist across boundary crossings.
            auto callback = entering ? trigger.onEnter : exiting ? trigger.onExit : std::function<void()>{};
            trigger.wasInside = inside;
            if (entering && trigger.oneShot) trigger.hasTriggered = true;
            if (callback) callback();
            // The callback can reallocate or replace the entire vector. No access
            // through 'trigger' is allowed from this point onward.
            if (generation != mGeneration) break;
        }
    }

    std::vector<TriggerParticleState> TriggerSystem::ParticleStates() const {
        std::map<size_t, bool> states;
        for (const auto& trigger : mTriggers) {
            if (trigger.particleIndex == kUnbound) continue;
            const bool active = trigger.isEnabled && trigger.wasInside && !(trigger.oneShot && trigger.hasTriggered);
            states[trigger.particleIndex] = states[trigger.particleIndex] || active;
        }
        std::vector<TriggerParticleState> result;
        result.reserve(states.size());
        for (const auto& state : states) result.push_back({state.first, state.second});
        return result;
    }
} // namespace engine
