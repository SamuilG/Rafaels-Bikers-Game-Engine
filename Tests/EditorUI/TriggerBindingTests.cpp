#include <functional>
#include "Runtime/Trigger/trigger.hpp"
#include "Runtime/Particle/ParticleSystem.hpp"
#include "Runtime/Debug/DebugRenderer.hpp"

#include <cstdio>
#include <cstdlib>
#include <limits>
#include <memory>
#include <vector>

// Default ParticleSystem instances have no GPU resources. Keep the test honest:
// any unexpected GPU cleanup is a failure, not a fake successful operation.
extern "C" void vmaDestroyBuffer(VmaAllocator, VkBuffer, VmaAllocation) {
    std::fputs("FAIL: trigger binding tests unexpectedly touched GPU resources\n", stderr);
    std::abort();
}

// TriggerSystem shares its translation unit with debug drawing. Satisfy those
// link-only boundaries without linking a renderer; entering one fails the test.
namespace engine {
    void DebugRenderer::DrawBox(const glm::vec3&, const glm::vec3&, const glm::vec3&, const glm::mat4&) {
        std::fputs("FAIL: trigger binding tests unexpectedly requested debug drawing\n", stderr);
        std::abort();
    }
    void DebugRenderer::DrawSphere(const glm::vec3&, float, const glm::vec3&, int) {
        std::fputs("FAIL: trigger binding tests unexpectedly requested debug drawing\n", stderr);
        std::abort();
    }
    void DebugRenderer::DrawCapsule(const glm::vec3&, float, float, const glm::vec3&, int) {
        std::fputs("FAIL: trigger binding tests unexpectedly requested debug drawing\n", stderr);
        std::abort();
    }
}

namespace {
    constexpr size_t kUnbound = std::numeric_limits<size_t>::max();

    void Require(bool condition, const char* message) {
        if (!condition) {
            std::fprintf(stderr, "FAIL: %s\n", message);
            std::exit(1);
        }
    }

    void TestRemovingBoundGroup() {
        engine::TriggerSystem triggers;
        triggers.AddBoxTrigger(glm::vec3(0), glm::vec3(1), 2);
        triggers.AddSphereTrigger(glm::vec3(0), 1.0f, 2);
        Require(triggers.HasParticleBinding(2), "multiple triggers can bind one group");
        triggers.OnParticleGroupRemoved(2);
        Require(!triggers.HasParticleBinding(2), "deleting a group detaches all triggers bound to it");
        Require(!triggers.HasParticleBinding(kUnbound), "detached bindings are not reported as a valid group");
        triggers.OnParticleGroupRemoved(0);
        Require(!triggers.HasParticleBinding(kUnbound - 1), "later deletions never decrement the unbound sentinel");
        std::puts("PASS deleted particle group detaches all bindings without sentinel underflow");
    }

    void TestIndexCompaction() {
        engine::TriggerSystem triggers;
        triggers.AddBoxTrigger(glm::vec3(0), glm::vec3(1), 1);
        triggers.AddCapsuleTrigger(glm::vec3(0), 0.5f, 1.0f, 4);
        triggers.AddSphereTrigger(glm::vec3(0), 1.0f, kUnbound);
        triggers.OnParticleGroupRemoved(2);
        Require(triggers.HasParticleBinding(1), "bindings before deleted index stay unchanged");
        Require(triggers.HasParticleBinding(3) && !triggers.HasParticleBinding(4),
            "bindings after deleted index shift left once");
        triggers.OnParticleGroupRemoved(0);
        Require(triggers.HasParticleBinding(0) && triggers.HasParticleBinding(2),
            "successive front deletions preserve remaining group identity");
        triggers.OnParticleGroupRemoved(kUnbound);
        Require(triggers.HasParticleBinding(0) && triggers.HasParticleBinding(2),
            "sentinel removal request is ignored");
        Require(!triggers.HasParticleBinding(kUnbound) && !triggers.HasParticleBinding(kUnbound - 1),
            "unbound trigger never becomes a real group binding");
        triggers.ClearTriggers();
        Require(!triggers.HasParticleBinding(0) && !triggers.HasParticleBinding(2), "scene clear removes bindings");
        std::puts("PASS particle index compaction, sentinel no-op and scene clear");
    }

    void TestDisabledAndCompletedTriggers() {
        engine::TriggerSystem disabled;
        const size_t disabledId = disabled.AddSphereTrigger(glm::vec3(0), 1.0f, 6);
        disabled.SetTriggerEnabled(disabledId, false);
        disabled.SetTriggerVisible(disabledId, false);
        Require(disabled.HasParticleBinding(6), "disabled invisible trigger still owns its binding");
        disabled.OnParticleGroupRemoved(2);
        Require(disabled.HasParticleBinding(5) && !disabled.HasParticleBinding(6),
            "disabled bindings are compacted too");

        engine::TriggerSystem oneShot;
        const size_t oneShotId = oneShot.AddSphereTrigger(glm::vec3(0), 1.0f, 0,
            glm::vec3(1), true, true);
        int entered = 0;
        oneShot.SetTriggerCallbacks(oneShotId, [&] { ++entered; });
        std::vector<std::unique_ptr<ParticleSystem>> particles;
        particles.push_back(std::make_unique<ParticleSystem>());
        oneShot.ProcessParticleTriggers(glm::vec3(0), particles);
        Require(entered == 1 && particles[0]->config.triggerControlled,
            "real trigger processing activates the particle and completes one-shot callback");
        oneShot.ProcessParticleTriggers(glm::vec3(10), particles);
        oneShot.ProcessParticleTriggers(glm::vec3(0), particles);
        Require(entered == 1, "completed one-shot does not fire again");
        Require(oneShot.HasParticleBinding(0), "completed one-shot still owns its saved binding");
        oneShot.OnParticleGroupRemoved(0);
        Require(!oneShot.HasParticleBinding(0), "completed one-shot binding detaches on deletion");
        std::puts("PASS disabled and actually completed one-shot bindings remain discoverable");
    }
}

int main() {
    TestRemovingBoundGroup();
    TestIndexCompaction();
    TestDisabledAndCompletedTriggers();
    std::puts("PASS all real TriggerSystem binding regressions (no GPU initialization)");
}
