#include "Physics/BikeLateralGrip.hpp"

#include <Jolt/Jolt.h>
#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Core/JobSystemSingleThreaded.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyLock.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

using namespace JPH;

static void Require(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        std::exit(1);
    }
}

#ifdef JPH_ENABLE_ASSERTS
static bool AssertImpl(const char* expression, const char* message, const char* file, uint line) {
    std::fprintf(stderr, "Jolt assert: %s:%u %s %s\n", file, line, expression, message ? message : "");
    std::abort();
}
#endif

class TestLayers final : public BroadPhaseLayerInterface {
public:
    uint GetNumBroadPhaseLayers() const override { return 1; }
    BroadPhaseLayer GetBroadPhaseLayer(ObjectLayer) const override { return BroadPhaseLayer(0); }
#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
    const char* GetBroadPhaseLayerName(BroadPhaseLayer) const override { return "test"; }
#endif
};
class TestBroadPhaseFilter final : public ObjectVsBroadPhaseLayerFilter {
public:
    bool ShouldCollide(ObjectLayer, BroadPhaseLayer) const override { return false; }
};
class TestPairFilter final : public ObjectLayerPairFilter {
public:
    bool ShouldCollide(ObjectLayer, ObjectLayer) const override { return false; }
};

// Use the engine's actual Jolt integration and damping, isolated from contacts.
class TestBike {
public:
    TestBike(float mass, Vec3Arg velocity) : allocator(10 * 1024 * 1024), jobs(1024) {
        physics.Init(16, 0, 16, 16, layers, broadPhaseFilter, pairFilter);
        physics.SetGravity(Vec3::sZero());
        BodyCreationSettings settings(new BoxShape(Vec3(0.25f, 0.5f, 1.0f)), RVec3::sZero(),
            Quat::sIdentity(), EMotionType::Dynamic, 0);
        settings.mOverrideMassProperties = EOverrideMassProperties::CalculateInertia;
        settings.mMassPropertiesOverride.mMass = mass;
        settings.mLinearDamping = 1.0f;
        settings.mAngularDamping = 10.0f;
        settings.mMotionQuality = EMotionQuality::LinearCast;
        settings.mAllowSleeping = false;
        settings.mLinearVelocity = velocity;
        id = Bodies().CreateAndAddBody(settings, EActivation::Activate);
        Require(!id.IsInvalid(), "create test bike");
    }

    ~TestBike() {
        Bodies().RemoveBody(id);
        Bodies().DestroyBody(id);
    }

    BodyInterface& Bodies() { return physics.GetBodyInterface(); }
    Vec3 Velocity() { return Bodies().GetLinearVelocity(id); }

    void ApplyGrip(Vec3Arg right, float dt) {
        float inverseMass = 0.0f;
        {
            BodyLockRead lock(physics.GetBodyLockInterface(), id);
            Require(lock.Succeeded(), "read live mass");
            inverseMass = lock.GetBody().GetMotionProperties()->GetInverseMass();
        }
        const float impulse = engine::CalculateBikeLateralGripImpulse(Velocity().Dot(right), inverseMass, dt);
        Bodies().AddImpulse(id, right * impulse);
    }

    void SetMass(float mass) {
        BodyLockWrite lock(physics.GetBodyLockInterface(), id);
        Require(lock.Succeeded(), "update live mass");
        lock.GetBody().GetMotionProperties()->SetInverseMass(1.0f / mass);
    }

    void Update(float dt) {
        Require(physics.Update(dt, 1, &allocator, &jobs) == EPhysicsUpdateError::None,
            "Jolt update succeeds");
    }

    BodyID id;

private:
    TestLayers layers;
    TestBroadPhaseFilter broadPhaseFilter;
    TestPairFilter pairFilter;
    PhysicsSystem physics;
    TempAllocatorImpl allocator;
    JobSystemSingleThreaded jobs;
};

static float FrameTime(int schedule, int frame) {
    constexpr float times[] = {1.0f / 60.0f, 1.0f / 30.0f, 0.05f};
    return times[schedule == 3 ? frame % 3 : schedule];
}

static void RequireStable(float before, float after) {
    constexpr float tolerance = 2.0e-6f;
    Require(std::isfinite(after), "lateral velocity remains finite");
    Require(std::abs(after) <= std::abs(before) + tolerance, "lateral velocity does not grow");
    Require(before >= 0.0f ? after >= -tolerance : after <= tolerance, "grip does not reverse lateral velocity");
}

static void TestDecay(float mass, int schedule, float initial, bool reduceMass) {
    TestBike bike(mass, Vec3(initial, 0.0f, 0.0f));
    for (int frame = 0; frame < 60; ++frame) {
        // Change mass while measurable slip remains, as the pickup does in game.
        if (reduceMass && frame == 2) bike.SetMass(mass * 0.5f);
        const float dt = FrameTime(schedule, frame);
        const float before = bike.Velocity().GetX();
        bike.ApplyGrip(Vec3::sAxisX(), dt);
        RequireStable(before, bike.Velocity().GetX());
        bike.Update(dt);
        RequireStable(before, bike.Velocity().GetX());
    }
    const float finalSpeed = std::abs(bike.Velocity().GetX());
    Require(finalSpeed < 1.0e-5f, "sideways slip converges to zero within 60 frames");
    std::printf("PASS decay mass=%.0f schedule=%d initial=%+.1f pickup=%s final=%.9g\n",
        mass, schedule, initial, reduceMass ? "yes" : "no", finalSpeed);
}

static void TestRotatedDirection(float initial) {
    const float yaw = 0.73f;
    const Vec3 right(std::cos(yaw), 0.0f, -std::sin(yaw));
    const Vec3 forward(-std::sin(yaw), 0.0f, -std::cos(yaw));
    TestBike bike(45.0f, right * initial + forward * 12.0f + Vec3::sAxisY() * 5.0f);
    for (int frame = 0; frame < 60; ++frame) {
        const float dt = FrameTime(3, frame);
        const Vec3 before = bike.Velocity();
        bike.ApplyGrip(right, dt);
        const Vec3 after = bike.Velocity();
        RequireStable(before.Dot(right), after.Dot(right));
        // Check before physics.Update, which intentionally damps all axes.
        Require(std::abs(after.Dot(forward) - before.Dot(forward)) < 3.0e-6f,
            "grip preserves forward velocity after rotation");
        Require(after.GetY() == before.GetY(), "grip preserves vertical jump/fall velocity");
        bike.Update(dt);
    }
    Require(std::abs(bike.Velocity().Dot(right)) < 1.0e-5f, "rotated sideways slip converges");
    std::printf("PASS rotated basis initial=%+.1f; forward and vertical impulse components preserved\n", initial);
}

static void TestInactiveInputs() {
    for (float speed : {-1.0f, 1.0f}) {
        Require(engine::CalculateBikeLateralGripImpulse(speed, 1.0f / 45.0f, 0.0f) == 0.0f,
            "zero dt gives no impulse");
        Require(engine::CalculateBikeLateralGripImpulse(speed, 1.0f / 45.0f, -0.05f) == 0.0f,
            "negative dt gives no impulse");
        Require(engine::CalculateBikeLateralGripImpulse(speed, 0.0f, 0.05f) == 0.0f,
            "zero inverse mass gives no impulse");
        Require(engine::CalculateBikeLateralGripImpulse(speed, -1.0f, 0.05f) == 0.0f,
            "negative inverse mass gives no impulse");
    }
    std::puts("PASS inactive dt and inverse mass");
}

static void TestOriginalForceDiverges() {
    TestBike bike(45.0f, Vec3(0.1f, 0.0f, 0.0f));
    int reversals = 0;
    float peak = 0.1f;
    for (int frame = 0; frame < 8; ++frame) {
        const float before = bike.Velocity().GetX();
        bike.Bodies().AddForce(bike.id, Vec3(-5000.0f * before, 0.0f, 0.0f));
        bike.Update(1.0f / 30.0f);
        const float after = bike.Velocity().GetX();
        if (before * after < 0.0f) ++reversals;
        peak = std::max(peak, std::abs(after));
    }
    Require(reversals == 8 && peak > 100.0f, "regression setup reproduces original force instability");
    std::printf("PASS sensitivity: original force at 45 kg / 30 FPS reverses %d times, peak %.3f m/s\n",
        reversals, peak);
}

int main() {
    RegisterDefaultAllocator();
    JPH_IF_ENABLE_ASSERTS(AssertFailed = AssertImpl;)
    Factory::sInstance = new Factory();
    RegisterTypes();

    TestInactiveInputs();
    TestOriginalForceDiverges();
    for (float mass : {90.0f, 45.0f})
        for (int schedule = 0; schedule < 4; ++schedule)
            for (float initial : {-1.0f, -0.1f, 0.1f, 1.0f})
                TestDecay(mass, schedule, initial, false);
    for (int schedule = 0; schedule < 4; ++schedule)
        for (float initial : {-1.0f, -0.1f, 0.1f, 1.0f})
            TestDecay(90.0f, schedule, initial, true);
    for (float initial : {-1.0f, 1.0f}) TestRotatedDirection(initial);

    UnregisterTypes();
    delete Factory::sInstance;
    Factory::sInstance = nullptr;
    std::puts("PASS all 50 Jolt grip scenarios (3000 frames), inactive inputs, and original-force sensitivity");
    return 0;
}
