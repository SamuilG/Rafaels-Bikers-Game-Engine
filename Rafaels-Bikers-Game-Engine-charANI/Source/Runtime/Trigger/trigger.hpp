#pragma once

#include <cstddef>
#include <glm/glm.hpp>
#include <memory>
#include <vector>

class ParticleSystem;

namespace engine {

    class DebugRenderer;

    enum class TriggerShape {
        Box,
        Sphere,
        Capsule
    };

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
        // trigger: remembers whether the probe was inside on the previous frame so we can print once on enter.
        bool wasInside = false;
        //进入触发触发回调// trigger: callback fired once when the probe enters this trigger volume.
        std::function<void()> onEnter;
        //离开触发回调// trigger: callback fired once when the probe exits this trigger volume.
        std::function<void()> onExit;
    };

    class TriggerSystem {
    public:
        //盒体触发器// trigger: create a box trigger
        size_t AddBoxTrigger(const glm::vec3& center, const glm::vec3& extents, size_t particleIndex, const glm::vec3& color = glm::vec3(1.0f, 0.0f, 0.0f), const glm::mat4& transform = glm::mat4(1.0f), bool isVisible = true, bool oneShot = false);

        //球体触发// trigger: create a sphere trigger
        size_t AddSphereTrigger(const glm::vec3& center, float radius, size_t particleIndex, const glm::vec3& color = glm::vec3(0.0f, 0.0f, 1.0f), bool isVisible = true, bool oneShot = false, int segments = 16);

        //胶囊体触发// trigger: create a capsule trigger 
        size_t AddCapsuleTrigger(const glm::vec3& center, float radius, float halfHeight, size_t particleIndex, const glm::vec3& color = glm::vec3(1.0f, 1.0f, 0.0f), bool isVisible = true, bool oneShot = false, int segments = 16);

        // trigger: toggle debug visibility without disabling the trigger logic itself.
        void SetTriggerVisible(size_t triggerId, bool isVisible);

        // trigger: enable or disable runtime collision checks for one trigger volume.
        void SetTriggerEnabled(size_t triggerId, bool isEnabled);

        // callback
        void SetTriggerCallbacks(size_t triggerId, std::function<void()> onEnter, std::function<void()> onExit = {});

        // trigger: clear all stored triggers when rebuilding or unloading a scene.
        void ClearTriggers();

        // trigger: test the probe position against every trigger and sync the result to particle visibility.
        void ProcessParticleTriggers(const glm::vec3& probePosition, std::vector<std::unique_ptr<ParticleSystem>>& particles);

        // trigger: draw visible trigger volumes through DebugRenderer so the trigger area can be inspected in-scene.
        void DrawTriggers(DebugRenderer& debugRenderer) const;

    private:
        // trigger: persistent trigger storage so the same trigger volumes can be reused across frames.
        std::vector<TriggerVolume> mTriggers;
    };

} // namespace engine
