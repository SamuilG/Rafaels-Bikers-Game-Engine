#include "Runtime/Gameplay/VehiclePresentation.hpp"
#include "Runtime/Scene/SceneRenderer.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <type_traits>
#include <utility>

using namespace engine;
static_assert(std::is_same_v<decltype(std::declval<VehiclePresentation&>().State()), const SceneEffects&>);

namespace {
constexpr float kDt = 1.0f / 60.0f;

void Require(bool condition, const char* message) {
    if (!condition) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}
void Near(float actual, float expected, const char* message) {
    Require(std::isfinite(actual) && std::abs(actual - expected) < 0.0001f, message);
}
void Same(const glm::vec4& actual, const glm::vec4& expected, const char* message) {
    for (int i = 0; i < 4; ++i) Near(actual[i], expected[i], message);
}
void Same(const glm::mat4& actual, const glm::mat4& expected, const char* message) {
    for (int i = 0; i < 4; ++i) Same(actual[i], expected[i], message);
}
FrameExecution Simulating() {
    FrameExecution frame;
    frame.runGameplay = true;
    frame.runSimulationSystems = true;
    return frame;
}
PortalTransitionVisual Visual() {
    PortalTransitionVisual result;
    result.active = true;
    result.bodyId = 7;
    result.entityId = 71;
    result.realClipPlane = glm::vec4(0, 0, 1, -0.025f);
    result.cloneClipPlane = glm::vec4(1, 0, 0, -10.025f);
    result.cloneMap = glm::translate(glm::mat4(1.0f), glm::vec3(10, 2, 5));
    return result;
}
RenderBatch Batch(std::uint64_t entity, std::uint32_t body, float x) {
    RenderBatch batch{};
    batch.meshIndex = static_cast<std::uint32_t>(entity);
    batch.materialIndex = 3;
    batch.entityId = entity;
    batch.compoundBodyID = body;
    batch.transform = glm::translate(glm::mat4(1.0f), glm::vec3(x, 0, 0));
    batch.ditherFade = 0.7f;
    return batch;
}

void TestSpeedAndDeathPresentation() {
    VehiclePresentation presentation;
    PlayerState player;
    LevelState level;
    const auto frame = Simulating();
    player.bikeSpeed = 20.0f;
    presentation.Update(kDt, player, level, frame, 71, 7);
    Near(presentation.State().speedFactor, 0.0f, "20 m/s is the previous speed-effect threshold");
    player.bikeSpeed = 30.0f;
    presentation.Update(kDt, player, level, frame, 71, 7);
    Near(presentation.State().speedFactor, 0.5f * 5.0f * kDt, "30 m/s targets half strength with the previous response");
    const float previous = presentation.State().speedFactor;
    player.bikeSpeed = -40.0f;
    player.isAlive = false;
    player.deathFactor = 0.65f;
    presentation.Update(kDt, player, level, frame, 71, 7);
    Near(presentation.State().speedFactor, previous + (1.0f - previous) * 5.0f * kDt,
        "40 m/s targets full strength using absolute speed");
    Require(!presentation.State().subjectAlive, "death state is published, not recomputed");
    Near(presentation.State().deathFactor, 0.65f, "death effect comes from the player owner");
    Near(presentation.State().portalEffectTime, 3.0f * kDt, "portal visual clock advances once per simulation update");
    presentation.Update(2.0f, player, level, frame, 71, 7);
    Near(presentation.State().speedFactor, 1.0f, "long frames do not overshoot visual strength");
}

void TestPauseReloadAndReset() {
    VehiclePresentation presentation;
    PlayerState player;
    player.bikeSpeed = 40;
    LevelState level;
    level.portalTransitionVisualActive = true;
    presentation.Update(kDt, player, level, Simulating(), 71, 7);
    const SceneEffects original = presentation.State();
    FrameExecution paused;
    player.bikeSpeed = 0;
    presentation.Update(5.0f, player, level, paused, 71, 7);
    Near(presentation.State().speedFactor, original.speedFactor, "paused speed effect is frozen");
    Near(presentation.State().portalEffectTime, original.portalEffectTime, "paused portal clock is frozen");
    auto reloaded = Simulating();
    reloaded.reloadHandled = true;
    level.portalTransitionVisualActive = false;
    presentation.Update(5.0f, player, level, reloaded, 0, UINT32_MAX);
    Require(presentation.State().portalTransition.active, "reload handling does not publish a partial frame");
    Near(presentation.State().speedFactor, original.speedFactor, "reload frame does not smooth speed");
    Near(presentation.State().portalEffectTime, original.portalEffectTime, "reload frame does not advance clock");
    presentation.ResetForNewRun();
    const auto& reset = presentation.State();
    Require(reset.subjectAlive && !reset.portalTransition.active && reset.portalTransition.entityId == 0 &&
        reset.portalTransition.bodyId == UINT32_MAX, "session reset drops all old target identities");
    Near(reset.speedFactor, 0, "session reset clears speed smoothing");
    Near(reset.deathFactor, 0, "session reset clears death effect");
    Near(reset.portalEffectTime, 0, "session reset clears portal clock");
    Same(reset.portalTransition.cloneMap, glm::mat4(1), "session reset clears portal mapping");
}

void TestPortalGeometry() {
    VehiclePresentation presentation;
    PlayerState player;
    LevelState level;
    level.portalTransitionVisualActive = true;
    level.portalTransitionEntrySurface = glm::scale(glm::mat4(1), glm::vec3(2, 3, 4));
    const glm::vec3 exitPosition(10, 2, 5);
    level.portalTransitionExitSurface = glm::translate(glm::mat4(1), exitPosition) *
        glm::rotate(glm::mat4(1), glm::half_pi<float>(), glm::vec3(0, 1, 0)) *
        glm::scale(glm::mat4(1), glm::vec3(4, 2, 3));
    presentation.Update(kDt, player, level, Simulating(), 71, 7);
    auto visual = presentation.State().portalTransition;
    Require(visual.active && visual.entityId == 71 && visual.bodyId == 7, "portal output keeps only opaque identities");
    Same(visual.realClipPlane, glm::vec4(0, 0, 1, -0.025f), "entry clipping retains the original 2.5cm bias");
    Same(visual.cloneClipPlane, glm::vec4(1, 0, 0, -10.025f), "scaled rotated exit uses a unit-normal clip plane");
    Same(visual.cloneMap * glm::vec4(0, 0, 1, 1), glm::vec4(9, 2, 5, 1),
        "entry to exit maps a forward point through a half turn without portal surface scale");
    level.portalTransitionRealAtExit = true;
    level.portalTransitionExitCorrection = glm::vec3(0.5f, 0, 1);
    presentation.Update(kDt, player, level, Simulating(), 71, 7);
    visual = presentation.State().portalTransition;
    Same(visual.realClipPlane, glm::vec4(1, 0, 0, -10.025f), "exit handoff swaps real clipping side");
    Same(visual.cloneClipPlane, glm::vec4(0, 0, 1, -0.025f), "exit handoff swaps clone clipping side");
    Same(visual.cloneMap * glm::vec4(exitPosition + level.portalTransitionExitCorrection, 1),
        glm::vec4(0, 0, 0, 1), "exit mapping removes world correction before returning to entry");
    level.portalTransitionVisualActive = false;
    presentation.Update(kDt, player, level, Simulating(), 71, 7);
    Require(!presentation.State().portalTransition.active, "portal visual clears when crossing ends");
}

void TestInvalidDelta() {
    VehiclePresentation presentation;
    PlayerState player;
    player.bikeSpeed = 40;
    LevelState level;
    for (float dt : {0.0f, -1.0f, std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()}) {
        presentation.Update(dt, player, level, Simulating(), 71, 7);
    }
    Near(presentation.State().speedFactor, 0, "invalid duration cannot poison speed smoothing");
    Near(presentation.State().portalEffectTime, 0, "invalid duration cannot poison portal clock");
    player.bikeSpeed = std::numeric_limits<float>::quiet_NaN();
    presentation.Update(kDt, player, level, Simulating(), 71, 7);
    Near(presentation.State().speedFactor, 0, "invalid speed is treated as zero");
}

void TestOpaqueClippingAndClones() {
    const auto visual = Visual();
    auto first = Batch(11, 7, 1);
    auto second = Batch(12, 7, 2);
    auto farPart = Batch(13, 7, 80);
    auto unrelated = Batch(20, 8, 4);
    unrelated.isPortalClone = true;
    unrelated.clipPlane = glm::vec4(1, 2, 3, 4);
    std::vector<RenderBatch> visible{first, second, unrelated};
    const std::vector<RenderBatch> unculled{first, second, farPart, unrelated};
    SceneRenderer::ApplyOpaquePortalTransition(visible, visual, unculled);
    Require(visible.size() == 6, "opaque clone sources include the culled compound part");
    Same(visible[0].clipPlane, visual.realClipPlane, "real opaque part is clipped");
    Same(visible[1].clipPlane, visual.realClipPlane, "all compound parts use the same clip");
    Same(visible[2].clipPlane, unrelated.clipPlane, "unrelated clone clipping is preserved");
    Same(visible[2].transform, unrelated.transform, "unrelated transform is preserved");
    for (std::size_t i = 3; i < visible.size(); ++i) {
        const auto& clone = visible[i];
        Require(clone.isPortalClone && !clone.castShadow && clone.compoundBodyID == 7,
            "clones keep grouping identity and never duplicate shadows");
        Same(clone.transform, visual.cloneMap * unculled[i - 3].transform, "clone receives exactly one portal map");
        Same(clone.clipPlane, visual.cloneClipPlane, "clones retain the opposite portal side");
        Near(clone.ditherFade, 0.7f, "clones retain original material fade");
    }
    SceneRenderer::ApplyOpaquePortalTransition(visible, visual, unculled);
    Require(visible.size() == 6, "opaque helper is idempotent when reapplying to cached portal batches");
    Same(visible[3].transform, visual.cloneMap * first.transform, "reapplication does not map a clone twice");
    std::vector<RenderBatch> allCulled;
    SceneRenderer::ApplyOpaquePortalTransition(allCulled, visual, unculled);
    Require(allCulled.size() == 3, "visible clones survive when every real compound part is culled");
    std::vector<RenderBatch> fallback{first};
    SceneRenderer::ApplyOpaquePortalTransition(fallback, visual, {});
    SceneRenderer::ApplyOpaquePortalTransition(fallback, visual, {});
    Require(fallback.size() == 2, "visible geometry is the idempotent fallback when no unculled source exists");
}

void TestSkinnedClonesAndInactiveEffects() {
    const auto visual = Visual();
    auto target = Batch(51, UINT32_MAX, 1);
    target.isSkinned = true;
    target.attachedEntityId = 71;
    target.boneBaseIndex = 123;
    auto unrelated = target;
    unrelated.entityId = 52;
    unrelated.attachedEntityId = 72;
    std::vector<RenderBatch> batches{target, unrelated};
    SceneRenderer::ApplySkinnedPortalTransition(batches, visual);
    SceneRenderer::ApplySkinnedPortalTransition(batches, visual);
    Require(batches.size() == 3, "attached skinned geometry has exactly one clone");
    Same(batches[0].clipPlane, visual.realClipPlane, "attached real geometry is clipped");
    Same(batches[1].clipPlane, unrelated.clipPlane, "other attachments are not clipped");
    Require(batches[2].isSkinned && batches[2].isPortalClone && !batches[2].castShadow &&
        batches[2].boneBaseIndex == 123, "skinned clone shares bone slice without shadow duplication");
    Same(batches[2].transform, visual.cloneMap * target.transform, "skinned clone maps once");
    std::vector<RenderBatch> inactive{target};
    SceneRenderer::ApplySkinnedPortalTransition(inactive, PortalTransitionVisual{});
    SceneRenderer::ApplyOpaquePortalTransition(inactive, PortalTransitionVisual{}, {target});
    Require(inactive.size() == 1 && !inactive[0].isPortalClone, "inactive effects leave batches intact");
    auto invalid = visual;
    invalid.bodyId = UINT32_MAX;
    invalid.entityId = 0;
    SceneRenderer::ApplySkinnedPortalTransition(inactive, invalid);
    SceneRenderer::ApplyOpaquePortalTransition(inactive, invalid, {target});
    Require(inactive.size() == 1, "unset identities never select unrelated geometry");
}

struct FakeScene final : SceneRenderSource {
    std::vector<RenderBatch> batches;
    bool requestedUnculled = false;
    bool updatesViewStats = true;
    std::vector<RenderBatch> BuildRenderBatches(const SceneRenderRequest& request) override {
        requestedUnculled = request.frustum == nullptr && request.frustumPadding == 0.0f;
        updatesViewStats = request.updateFrustumStats;
        return batches;
    }
    std::vector<RenderBatch> BuildSkinnedBatches(glm::mat4*, std::size_t, std::size_t& boneCount) override {
        boneCount = 0; return {};
    }
    void CollectLights(std::vector<GpuLight>& lights) override { lights.clear(); }
    std::uint32_t LastFrustumCandidates() const override { return 0; }
    std::uint32_t LastFrustumVisible() const override { return 0; }
};

void TestSceneSourceContract() {
    FakeScene source;
    auto oldClone = Batch(11, 7, 0);
    oldClone.isPortalClone = true;
    source.batches = {Batch(11, 7, 1), Batch(12, 8, 2), oldClone};
    SceneRenderer renderer(&source);
    const auto sources = renderer.PortalCloneSources(Visual(), glm::vec3(0));
    Require(source.requestedUnculled, "clone preparation bypasses real-view frustum culling");
    Require(!source.updatesViewStats, "auxiliary clone queries preserve view statistics");
    Require(sources.size() == 1 && sources[0].entityId == 11 && !sources[0].isPortalClone,
        "clone preparation filters target body and ignores earlier clones");
    auto noShadow = Batch(13, 9, 0);
    noShadow.castShadow = false;
    source.batches.push_back(noShadow);
    const auto casters = renderer.BuildShadowCasters(glm::vec3(0));
    Require(source.requestedUnculled && !source.updatesViewStats && casters.size() == 2,
        "shadow preparation is unculled, preserves statistics and excludes non-casters and portal clones");
    SceneRenderer noScene;
    Require(noScene.PortalCloneSources(Visual(), glm::vec3(0)).empty(), "empty host safely has no clone sources");
}
} // namespace

int main() {
    struct Test { const char* name; void (*run)(); };
    const Test tests[] = {
        {"speed and death outputs", TestSpeedAndDeathPresentation},
        {"pause reload reset", TestPauseReloadAndReset},
        {"portal geometry and exit correction", TestPortalGeometry},
        {"invalid frame durations", TestInvalidDelta},
        {"opaque clipping far sources and idempotence", TestOpaqueClippingAndClones},
        {"skinned attachments and inactive effects", TestSkinnedClonesAndInactiveEffects},
        {"replaceable scene source without ECS or GPU", TestSceneSourceContract},
    };
    for (const auto& test : tests) { test.run(); std::printf("PASS: %s\n", test.name); }
    std::printf("All %zu vehicle presentation groups passed.\n", sizeof(tests) / sizeof(tests[0]));
}
