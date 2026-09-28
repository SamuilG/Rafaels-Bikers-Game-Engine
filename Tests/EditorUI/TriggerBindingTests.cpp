#include "Runtime/Trigger/trigger.hpp"
#include <glm/gtc/matrix_transform.hpp>

#include <cstdio>
#include <cstdlib>
#include <limits>

namespace {
    constexpr size_t kUnbound = std::numeric_limits<size_t>::max();

    void Require(bool condition, const char* message) {
        if (!condition) {
            std::fprintf(stderr, "FAIL: %s\n", message);
            std::exit(1);
        }
    }

    bool Active(const engine::TriggerSystem& triggers, size_t particleIndex) {
        for (const auto& state : triggers.ParticleStates())
            if (state.particleIndex == particleIndex) return state.active;
        return false;
    }

    void TestNoParticlesAndCrossings() {
        engine::TriggerSystem triggers;
        int entered = 0, exited = 0;
        const auto id = triggers.AddSphereTrigger(glm::vec3(0), 1.0f, kUnbound);
        triggers.SetTriggerVisible(id, false);
        triggers.SetTriggerCallbacks(id,
            [&, count = 0]() mutable { entered = ++count; },
            [&, count = 0]() mutable { exited = ++count; });
        triggers.Update(glm::vec3(10));
        triggers.Update(glm::vec3(0));
        triggers.Update(glm::vec3(0));
        triggers.Update(glm::vec3(10));
        triggers.Update(glm::vec3(10));
        triggers.Update(glm::vec3(0));
        Require(entered == 2 && exited == 1, "callbacks fire only on crossings without any particle system");
        Require(triggers.ParticleStates().empty(), "unbound triggers have no particle output");
        Require(!triggers.Volumes()[id].isVisible && triggers.Volumes()[id].wasInside,
            "hidden debug volumes continue simulation");
        triggers.Update(glm::vec3(10));
        Require(entered == 2 && exited == 2, "mutable value captures persist independently for enter and exit callbacks");
        std::puts("PASS unbound hidden triggers enter and exit with no particle or renderer dependency");
    }

    void TestGeometry() {
        engine::TriggerSystem triggers;
        const glm::mat4 transform = glm::translate(glm::mat4(1), glm::vec3(10, 0, 0));
        triggers.AddBoxTrigger(glm::vec3(0), glm::vec3(1), 1, glm::vec3(1), transform);
        triggers.AddCapsuleTrigger(glm::vec3(0), 0.5f, 2.0f, 2);
        triggers.AddCapsuleTrigger(glm::vec3(5, 0, 0), 1.0f, 0.0f, 3);
        triggers.Update(glm::vec3(11, 1, 1));
        Require(Active(triggers, 1), "transformed box includes boundary in local space");
        triggers.Update(glm::vec3(11.01f, 1, 1));
        Require(!Active(triggers, 1), "transformed box excludes outside point");
        triggers.Update(glm::vec3(0, 2.5f, 0));
        Require(Active(triggers, 2), "capsule includes rounded tip boundary");
        triggers.Update(glm::vec3(0, 2.51f, 0));
        Require(!Active(triggers, 2), "capsule excludes point beyond tip");
        triggers.Update(glm::vec3(6, 0, 0));
        Require(Active(triggers, 3), "zero-height capsule behaves as a sphere without division by zero");
        std::puts("PASS transformed box, capsule boundary and zero-height capsule geometry");
    }

    void TestOneShot() {
        engine::TriggerSystem triggers;
        const auto id = triggers.AddSphereTrigger(glm::vec3(0), 1, 0, glm::vec3(1), true, true);
        triggers.Update(glm::vec3(0));
        Require(triggers.Volumes()[id].hasTriggered, "one-shot without callback still completes");
        Require(triggers.HasParticleBinding(0) && !Active(triggers, 0),
            "completed one-shot retains an inactive particle binding immediately");
        int entered = 0, exited = 0;
        triggers.SetTriggerCallbacks(id, [&] { ++entered; }, [&] { ++exited; });
        triggers.Update(glm::vec3(10));
        triggers.Update(glm::vec3(0));
        Require(entered == 0 && exited == 0, "completed one-shot cannot fire after attaching callbacks");
        const auto callbackId = triggers.AddSphereTrigger(glm::vec3(0), 1, 1, glm::vec3(1), true, true);
        triggers.SetTriggerCallbacks(callbackId, [&] {
            ++entered;
            Require(triggers.Volumes()[callbackId].hasTriggered, "one-shot commits before callback");
        }, [&] { ++exited; });
        triggers.Update(glm::vec3(0));
        triggers.Update(glm::vec3(10));
        triggers.Update(glm::vec3(0));
        Require(entered == 1 && exited == 0 && !Active(triggers, 1), "callback one-shot completes exactly once");
        std::puts("PASS one-shot lifecycle with and without callbacks, with persistent inactive binding");
    }

    void TestDisabledBindings() {
        engine::TriggerSystem triggers;
        int entered = 0, exited = 0;
        const auto id = triggers.AddSphereTrigger(glm::vec3(0), 1, 6);
        triggers.SetTriggerCallbacks(id, [&] { ++entered; }, [&] { ++exited; });
        triggers.Update(glm::vec3(0));
        Require(Active(triggers, 6), "enabled occupied trigger emits active output");
        triggers.SetTriggerEnabled(id, false);
        Require(triggers.HasParticleBinding(6) && !Active(triggers, 6), "disable immediately suppresses particle output");
        triggers.Update(glm::vec3(0));
        triggers.Update(glm::vec3(10));
        Require(entered == 1 && exited == 0, "disable cancels crossing without synthetic exit");
        triggers.SetTriggerEnabled(id, true);
        triggers.Update(glm::vec3(0));
        Require(entered == 2 && Active(triggers, 6), "re-enabled trigger starts a fresh crossing");
        std::puts("PASS disabled bindings suppress output and restart crossing when re-enabled");
    }

    void TestAggregateBindings() {
        engine::TriggerSystem triggers;
        const auto first = triggers.AddSphereTrigger(glm::vec3(0), 1, 1000);
        const auto second = triggers.AddSphereTrigger(glm::vec3(5, 0, 0), 1, 1000);
        triggers.AddSphereTrigger(glm::vec3(0), 1, 2, glm::vec3(1), true, true);
        triggers.Update(glm::vec3(0));
        const auto states = triggers.ParticleStates();
        Require(states.size() == 2 && states[0].particleIndex == 2 && states[1].particleIndex == 1000,
            "outputs are unique and sorted without needing particle collection size");
        Require(!states[0].active && states[1].active, "one-shot inactive and ordinary active results coexist");
        triggers.Update(glm::vec3(5, 0, 0));
        Require(Active(triggers, 1000), "second bound trigger can independently activate particle");
        triggers.SetTriggerEnabled(first, false);
        Require(Active(triggers, 1000), "disabled sibling does not override active OR result");
        triggers.SetTriggerEnabled(second, false);
        Require(!Active(triggers, 1000) && triggers.ParticleStates().size() == 2,
            "all-disabled binding emits explicit inactive result");
        std::puts("PASS particle outputs aggregate OR across bindings independently of particle collection");
    }

    void TestRemovingBoundGroup() {
        engine::TriggerSystem triggers;
        triggers.AddBoxTrigger(glm::vec3(0), glm::vec3(1), 2);
        triggers.AddSphereTrigger(glm::vec3(0), 1.0f, 2);
        triggers.Update(glm::vec3(0));
        triggers.OnParticleGroupRemoved(2);
        Require(!triggers.HasParticleBinding(2) && triggers.ParticleStates().empty(),
            "deleting a group detaches all triggers and all output for it");
        Require(!triggers.HasParticleBinding(kUnbound), "detached bindings are not real group bindings");
        triggers.OnParticleGroupRemoved(0);
        Require(!triggers.HasParticleBinding(kUnbound - 1), "later removals never decrement unbound sentinel");
        std::puts("PASS deleted particle group detaches every binding without sentinel underflow");
    }

    void TestIndexCompaction() {
        engine::TriggerSystem triggers;
        triggers.AddBoxTrigger(glm::vec3(0), glm::vec3(1), 1);
        const auto disabled = triggers.AddCapsuleTrigger(glm::vec3(0), 0.5f, 1, 4);
        triggers.SetTriggerEnabled(disabled, false);
        triggers.AddSphereTrigger(glm::vec3(0), 1, 5, glm::vec3(1), true, true);
        triggers.AddSphereTrigger(glm::vec3(0), 1, kUnbound);
        triggers.Update(glm::vec3(0));
        triggers.OnParticleGroupRemoved(2);
        Require(triggers.HasParticleBinding(1) && triggers.HasParticleBinding(3) && triggers.HasParticleBinding(4),
            "removal compacts live, disabled and completed bindings");
        triggers.OnParticleGroupRemoved(0);
        triggers.OnParticleGroupRemoved(kUnbound);
        Require(triggers.HasParticleBinding(0) && triggers.HasParticleBinding(2) && triggers.HasParticleBinding(3),
            "repeated removals preserve identity and sentinel removal does nothing");
        Require(Active(triggers, 0) && !Active(triggers, 2) && !Active(triggers, 3),
            "compaction preserves output state");
        triggers.ClearTriggers();
        Require(triggers.Volumes().empty() && triggers.ParticleStates().empty() && !triggers.HasParticleBinding(0),
            "clear removes geometry, bindings and output state");
        std::puts("PASS index compaction includes disabled and completed triggers, clear resets snapshots");
    }

    void TestCallbackAddsTriggers() {
        engine::TriggerSystem triggers;
        int addedEnters = 0;
        const auto first = triggers.AddSphereTrigger(glm::vec3(0), 1, kUnbound);
        triggers.SetTriggerCallbacks(first, [&] {
            for (size_t i = 0; i < 64; ++i) {
                const auto id = triggers.AddSphereTrigger(glm::vec3(0), 1, i);
                triggers.SetTriggerCallbacks(id, [&] { ++addedEnters; });
            }
        });
        triggers.Update(glm::vec3(0));
        Require(triggers.Volumes().size() == 65 && addedEnters == 0,
            "callback can reallocate vector; newly added triggers are deferred");
        triggers.Update(glm::vec3(0));
        Require(addedEnters == 64, "callback additions enter on next update exactly once");
        triggers.Update(glm::vec3(0));
        Require(addedEnters == 64, "adding triggers did not damage original crossing state");
        std::puts("PASS callback additions remain safe across reallocations and execute next update");
    }

    void TestCallbackClearsTriggers() {
        engine::TriggerSystem triggers;
        int staleEnters = 0, freshEnters = 0;
        const auto first = triggers.AddSphereTrigger(glm::vec3(0), 1, 0);
        const auto stale = triggers.AddSphereTrigger(glm::vec3(0), 1, 1);
        triggers.SetTriggerCallbacks(stale, [&] { ++staleEnters; });
        triggers.SetTriggerCallbacks(first, [&] {
            triggers.ClearTriggers();
            const auto fresh = triggers.AddSphereTrigger(glm::vec3(0), 1, 2);
            triggers.SetTriggerCallbacks(fresh, [&] { ++freshEnters; });
        });
        triggers.Update(glm::vec3(0));
        Require(triggers.Volumes().size() == 1 && staleEnters == 0 && freshEnters == 0,
            "callback clear cancels stale evaluation and defers replacement collection");
        Require(!triggers.HasParticleBinding(0) && !triggers.HasParticleBinding(1) && triggers.HasParticleBinding(2),
            "clear inside callback leaves only replacement bindings");
        triggers.Update(glm::vec3(0));
        Require(freshEnters == 1 && Active(triggers, 2), "replacement trigger starts next frame safely");
        std::puts("PASS callback clear invalidates old evaluation and safely replaces the trigger collection");
    }

    void TestCallbackDisablesAndReplaces() {
        engine::TriggerSystem triggers;
        int firstEnters = 0, secondEnters = 0;
        const auto first = triggers.AddSphereTrigger(glm::vec3(0), 1, 0);
        const auto second = triggers.AddSphereTrigger(glm::vec3(0), 1, 1);
        triggers.SetTriggerCallbacks(second, [&] { ++secondEnters; });
        triggers.SetTriggerCallbacks(first, [&] {
            ++firstEnters;
            triggers.SetTriggerEnabled(first, false);
            triggers.SetTriggerEnabled(second, false);
            triggers.SetTriggerCallbacks(first, [&, count = 0]() mutable { firstEnters += (++count) * 10; });
            triggers.Update(glm::vec3(0));
        });
        triggers.Update(glm::vec3(0));
        Require(firstEnters == 1 && secondEnters == 0 && !Active(triggers, 0) && !Active(triggers, 1),
            "callback can disable current/next trigger and replace itself, nested update does not recur");
        triggers.SetTriggerEnabled(second, true);
        triggers.Update(glm::vec3(0));
        Require(secondEnters == 1 && Active(triggers, 1), "disabled next trigger can enter when re-enabled later");
        triggers.SetTriggerEnabled(first, true);
        triggers.Update(glm::vec3(0));
        triggers.Update(glm::vec3(10));
        triggers.Update(glm::vec3(0));
        Require(firstEnters == 31 && secondEnters == 2 && Active(triggers, 0),
            "self-replacement remains installed and preserves its own mutable capture across later entries");
        std::puts("PASS callbacks safely disable current/next trigger, replace themselves and reject nested update");
    }
}

int main() {
    TestNoParticlesAndCrossings();
    TestGeometry();
    TestOneShot();
    TestDisabledBindings();
    TestAggregateBindings();
    TestRemovingBoundGroup();
    TestIndexCompaction();
    TestCallbackAddsTriggers();
    TestCallbackClearsTriggers();
    TestCallbackDisablesAndReplaces();
    std::puts("PASS all 10 real TriggerSystem regression groups (CPU and GLM only)");
}
