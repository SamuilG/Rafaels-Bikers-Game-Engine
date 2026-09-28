#pragma once

#include <cstddef>
#include <functional>
#include <glm/glm.hpp>
#include <vector>

namespace engine {

    enum class TriggerShape { Box, Sphere, Capsule };

    struct TriggerVolume {
        TriggerShape shape = TriggerShape::Box;
        glm::vec3 center = glm::vec3(0.0f);
        glm::vec3 extents = glm::vec3(0.5f);
        glm::vec3 color = glm::vec3(0.0f, 1.0f, 0.0f);
        glm::mat4 transform = glm::mat4(1.0f);
        float radius = 0.5f;
        float halfHeight = 0.5f;
        int segments = 16;
        size_t particleIndex = static_cast<size_t>(-1);
        bool isVisible = true;
        bool isEnabled = true;
        bool oneShot = false;
        bool hasTriggered = false;
        bool wasInside = false;
        std::function<void()> onEnter;
        std::function<void()> onExit;
    };

    struct TriggerParticleState {
        size_t particleIndex;
        bool active;
    };

    // CPU trigger evaluation. The scene bridges particle outputs; rendering only
    // reads volume geometry. Neither operation participates in trigger execution.
    class TriggerSystem {
    public:
        size_t AddBoxTrigger(const glm::vec3& center, const glm::vec3& extents, size_t particleIndex, const glm::vec3& color = glm::vec3(1.0f, 0.0f, 0.0f), const glm::mat4& transform = glm::mat4(1.0f), bool isVisible = true, bool oneShot = false);
        size_t AddSphereTrigger(const glm::vec3& center, float radius, size_t particleIndex, const glm::vec3& color = glm::vec3(0.0f, 0.0f, 1.0f), bool isVisible = true, bool oneShot = false, int segments = 16);
        size_t AddCapsuleTrigger(const glm::vec3& center, float radius, float halfHeight, size_t particleIndex, const glm::vec3& color = glm::vec3(1.0f, 1.0f, 0.0f), bool isVisible = true, bool oneShot = false, int segments = 16);

        // Debug visibility has no effect on simulation.
        void SetTriggerVisible(size_t triggerId, bool isVisible);
        // Disabling resets the crossing state without emitting an exit callback.
        void SetTriggerEnabled(size_t triggerId, bool isEnabled);
        // Mutable captures persist across crossings; replacing or clearing a
        // callback during its own execution keeps the active callable alive.
        void SetTriggerCallbacks(size_t triggerId, std::function<void()> onEnter, std::function<void()> onExit = {});
        void ClearTriggers();
        void OnParticleGroupRemoved(size_t particleIndex);
        // Binding ownership survives disabling and completion of one-shot triggers.
        bool HasParticleBinding(size_t particleIndex) const;

        // A frame evaluates only triggers present at entry. Callback additions are
        // evaluated next frame; clearing immediately ends this evaluation. Nested
        // Update calls are ignored. Callbacks may safely change the trigger list.
        void Update(const glm::vec3& probePosition);
        const std::vector<TriggerVolume>& Volumes() const { return mTriggers; }
        // One result per bound index, in index order. Disabled/completed bindings
        // remain present with active=false; multiple bindings combine with OR.
        std::vector<TriggerParticleState> ParticleStates() const;

    private:
        std::vector<TriggerVolume> mTriggers;
        size_t mGeneration = 0;
        bool mUpdating = false;
    };

} // namespace engine
