#include "Runtime/Gameplay/VehiclePresentation.hpp"
#include "Runtime/Renderer/RenderUtilities/PortalGroundSlabs.hpp"
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

void TestPortalOpeningShards() {
    VehiclePresentation presentation;
    PlayerState player;
    LevelState level;
    const auto frame = Simulating();
    const glm::vec3 origin(12.0f, 4.0f, -7.0f);
    player.position = origin;
    PortalShardEmitter effect;
    Require(effect.followRider, "opening a portal uses the rider's position by default");
    const auto shardsAtPlayer = [&] {
        std::vector<PortalShardVisual> shards;
        AppendPortalShardVisuals(effect, presentation.State().portalOpeningAge,
            glm::translate(glm::mat4(1.0f), player.position), shards);
        return shards;
    };
    presentation.Update(kDt, player, level, frame, 71, 7);
    Require(shardsAtPlayer().empty(), "no shards before the portal opens");

    presentation.BeginPortalOpening();
    presentation.Update(0.75f, player, level, frame, 71, 7);
    const auto initial = shardsAtPlayer();
    Require(initial.size() == 12 && initial.front().material.opacity > 0.0f, "opening spawns 12 visible shards");
    Near(glm::length(glm::vec3(initial.front().transform[0])), 3.59f, "the smallest shard uses the new default size");
    const glm::vec3 initialOffset = glm::vec3(initial.front().transform[3]) - origin;
    Require(initialOffset.y > 0.0f && glm::length(initialOffset) < 5.0f,
        "shards originate in the space around the rider");

    const glm::vec3 moved = origin + glm::vec3(24.0f, 0.0f, 0.0f);
    player.position = moved;
    presentation.Update(1.0f, player, level, FrameExecution{}, 71, 7);
    Near(presentation.State().portalOpeningAge, 0.75f, "the portal opening clock freezes while paused");
    presentation.Update(2.0f, player, level, frame, 71, 7);
    const auto expanded = shardsAtPlayer();
    Require(expanded.size() == 12, "the full burst remains visible more than two seconds after opening");
    const glm::vec3 expandedOffset = glm::vec3(expanded.front().transform[3]) - moved;
    Require(glm::length(expandedOffset) < 6.0f && glm::length(expandedOffset) > glm::length(initialOffset),
        "shards travel with the rider as the burst expands");
    float outerRadius = 0.0f;
    float highestShard = 0.0f;
    for (const auto& shard : expanded) {
        const glm::vec3 offset = glm::vec3(shard.transform[3]) - moved;
        outerRadius = std::max(outerRadius, glm::length(glm::vec2(offset.x, offset.z)));
        highestShard = std::max(highestShard, offset.y);
    }
    Require(outerRadius > 7.8f && outerRadius <= 8.2f && highestShard > 5.0f,
        "shards expand across a wider and taller area around the rider");
    const float fullOpacity = expanded.front().material.opacity;
    presentation.Update(5.5f, player, level, frame, 71, 7);
    const auto fading = shardsAtPlayer();
    Require(!fading.empty() && fading.front().material.opacity > 0.0f && fading.front().material.opacity < fullOpacity,
        "shards are still visible while fading after eight seconds");
    presentation.Update(0.6f, player, level, frame, 71, 7);
    Require(shardsAtPlayer().empty(), "shards expire after 8.8 seconds");

    presentation.BeginPortalOpening();
    presentation.Update(0.5f, player, level, frame, 71, 7);
    Require(!shardsAtPlayer().empty(), "a new deployment restarts emission");
    presentation.CancelPortalOpening();
    presentation.Update(kDt, player, level, frame, 71, 7);
    Require(shardsAtPlayer().empty(), "closing or canceling a portal clears the burst");

    effect.count = 1;
    effect.emissionDuration = effect.fadeIn = effect.fadeOut = 0.0f;
    effect.spawnRadius = glm::vec2(4.0f);
    effect.spawnHeight = glm::vec2(0.0f);
    effect.expansionSpeed = effect.orbitSpeed = 0.0f;
    effect.material.opacity = 0.35f;
    effect.material.ior = 1.9f;
    effect.duration = 1.25f;
    presentation.BeginPortalOpening();
    presentation.Update(0.5f, player, level, frame, 71, 7);
    const auto edited = shardsAtPlayer();
    Require(edited.size() == 1, "the authored count supports a single shard without dividing by zero");
    Same(edited.front().transform[3], glm::vec4(moved + glm::vec3(4.0f, 0.0f, 0.0f), 1.0f),
        "the authored radius and motion parameters determine placement");
    Near(edited.front().material.opacity, 0.35f, "the authored opacity reaches the rendered shard");
    Near(edited.front().material.ior, 1.9f, "the authored refraction index reaches the rendered shard");
    presentation.Update(0.75f, player, level, frame, 71, 7);
    Require(shardsAtPlayer().empty(), "an authored lifetime replaces the default");
}

void TestPortalScreenFilter() {
    VehiclePresentation presentation;
    PlayerState player;
    LevelState level;
    const auto frame = Simulating();
    PortalShardEmitter emitter;
    const auto amount = [&] { return emitter.screenFilter.Amount(presentation.State().portalFilter); };

    Near(amount(), 0.0f, "filter starts inactive");
    presentation.BeginPortalOpening();
    presentation.SetPortalOpeningProgress(0.25f);
    Near(amount(), emitter.screenFilter.strength * 0.25f, "filter follows the eased opening fraction");
    presentation.SetPortalOpeningProgress(1.0f);
    presentation.Update(PortalShardEmitter::kMaxDuration + 1.0f, player, level, frame, 71, 7);
    Require(presentation.State().portalOpeningAge < 0.0f, "shards expire while the portal stays open");
    Near(amount(), emitter.screenFilter.strength, "open portal retains its filter until traversal");

    presentation.FadePortalFilter();
    Near(amount(), emitter.screenFilter.strength, "traversal starts fading from the current amount");
    presentation.Update(0.6f, player, level, FrameExecution{}, 71, 7);
    Near(amount(), emitter.screenFilter.strength, "pausing freezes the fade");
    presentation.Update(0.6f, player, level, frame, 71, 7);
    Near(amount(), emitter.screenFilter.strength * 0.5f, "filter is halfway through the default fade");
    presentation.FadePortalFilter();
    presentation.SetPortalOpeningProgress(1.0f);
    presentation.Update(0.6f, player, level, frame, 71, 7);
    Near(amount(), 0.0f, "repeat traversal does not restart the fade");

    presentation.BeginPortalOpening();
    presentation.SetPortalOpeningProgress(0.5f);
    emitter.screenFilter.fadeOut = 2.0f;
    presentation.CancelPortalOpening();
    presentation.Update(1.0f, player, level, frame, 71, 7);
    Near(amount(), emitter.screenFilter.strength * 0.25f, "cancellation fades from partial strength using the emitter time");
    emitter.screenFilter.enabled = false;
    Near(amount(), 0.0f, "the emitter can disable its screen filter");
    emitter.screenFilter.enabled = true;
    player.isAlive = false;
    presentation.Update(kDt, player, level, frame, 71, 7);
    Near(amount(), 0.0f, "death removes the portal filter");
}

void TestPortalCrossingWave() {
    VehiclePresentation presentation;
    PlayerState player;
    LevelState level;
    const auto frame = Simulating();
    PortalWaveSettings settings;
    const auto wave = [&] { return settings.Visual(presentation.State().portalWave); };

    presentation.BeginPortalOpening();
    presentation.SetPortalOpeningProgress(1.0f);
    Near(wave().strength, 0.0f, "opening alone does not trigger a shockwave");
    presentation.CancelPortalOpening();
    Near(wave().strength, 0.0f, "closing a portal does not trigger a shockwave");

    const glm::vec3 feet(21.0f, 4.0f, -12.0f);
    presentation.OnPortalTraversal(feet);
    presentation.Update(0.12f, player, level, frame, 71, 7);
    Require(wave().origin == feet, "first frame uses the feet at the exit");
    Near(wave().radius, 0.4f, "the first rendered frame starts near the feet");
    Near(wave().strength, settings.strength, "the pulse starts on the crossing frame");
    Near(wave().groundLift, 1.05f, "the default ground fragments receive a stronger lift");

    player.position += glm::vec3(0.0f, 0.0f, 7.0f);
    presentation.Update(0.35f, player, level, frame, 71, 7);
    const float radius = wave().radius;
    const float earlyLift = wave().groundLift;
    Require(radius > 0.4f && wave().origin == feet, "moving the rider leaves the expanding sphere at the crossing");
    presentation.Update(0.5f, player, level, FrameExecution{}, 71, 7);
    Near(wave().radius, radius, "pause freezes the wavefront");
    presentation.Update(0.5f, player, level, frame, 71, 7);
    Require(wave().radius > radius && wave().strength == settings.strength, "the growing wave holds its strength longer");
    Require(wave().groundLift < earlyLift, "outward propagation lowers the lifted fragments");
    presentation.Update(0.65f, player, level, frame, 71, 7);
    Require(wave().radius > 28.0f && wave().strength > 0.0f && wave().strength < settings.strength,
        "the wave reaches beyond its previous range while dissolving");
    presentation.Update(0.70f, player, level, frame, 71, 7);
    Require(wave().radius > 37.0f && wave().groundLift < 0.02f,
        "the far wavefront remains visible while the slab motion settles back into the floor");
    presentation.Update(0.20f, player, level, frame, 71, 7);
    Near(wave().strength, 0.0f, "wave vanishes after its authored duration");

    const glm::vec3 nextExit(-3.0f, 1.0f, -4.0f);
    presentation.OnPortalTraversal(nextExit);
    Require(wave().origin == nextExit, "each traversal launches a new wave at its exit");
    settings.enabled = false;
    Near(wave().strength, 0.0f, "the entity can disable the wave");
    settings.enabled = true;
    presentation.ResetForNewRun();
    Near(wave().strength, 0.0f, "restart clears a live wave");
}

void TestPortalGroundSurface() {
    PortalWaveVisual wave;
    wave.origin = {12.0f, 3.0f, -7.0f};
    wave.maxRadius = 16.0f;
    wave.width = 1.8f;
    EngineModel model;
    auto& floor = model.meshes.emplace_back();
    floor.positions = {{-4, -2, -4}, {4, -1, -4}, {4, -1, 4}, {-4, -2, 4}};
    floor.texcoords = {{0, 0}, {2, 0}, {2, 1}, {0, 1}};
    floor.indices = {0, 1, 2, 0, 2, 3};
    floor.localAabbMin = {-4, -2, -4};
    floor.localAabbMax = {4, -1, 4};
    const auto translate = [&](glm::vec3 offset) {
        return glm::translate(glm::mat4(1), wave.origin + offset);
    };
    RenderBatch support{0, 0, translate({0, 0, 0})};
    RenderBatch ceiling{0, 0, translate({0, 4, 0})};
    RenderBatch rider{0, 0, translate({0, 1, 0})};
    rider.compoundBodyID = 7;
    const std::array batches{support, ceiling, rider};
    PortalGroundSurface ground;
    Require(ground.Build(wave, batches, model, 7), "a coarse ramp still generates dense ground support");
    const int mid = PortalGroundSurface::kResolution / 2;
    Near(ground.heights[mid * PortalGroundSurface::kResolution + mid],
        -1.5f + ground.spacing * 0.0625f, "sampled height matches the ramp and ignores the roof and rider");
    Require(ground.heights.front() == PortalGroundSurface::kMissing, "unsupported tiles across a gap stay absent");
    Require(!ground.Build(wave, std::span(batches).subspan(1), model, 7),
        "ceilings and the crossing vehicle alone do not generate ground fragments");

    const RenderBatch inlay{0, 4, translate({4.0f, 0.82f, 0.0f}) *
        glm::scale(glm::mat4(1.0f), glm::vec3(0.6f, 1.0f, 1.0f))};
    const std::array layered{support, inlay, ceiling, rider};
    const auto grid = [&](glm::vec2 point) {
        return point / ground.spacing + float(PortalGroundSurface::kResolution - 1) * 0.5f;
    };
    const std::array materialPoints{grid({-2.0f, 1.0f}), grid({2.2f, 1.0f}), grid({12.0f, 1.0f})};
    Require(ground.Build(wave, layered, model, 7, materialPoints), "ground material samples preserve height support");
    Require(ground.materials[0].materialIndex == 0 && ground.materials[1].materialIndex == 4 &&
        ground.materials[2].materialIndex == UINT32_MAX, "each slab chooses its actual topmost supporting material");
    Near(ground.materials[0].uv.x, 0.5f, "the original ground U uses barycentric interpolation");
    Near(ground.materials[0].uv.y, 0.625f, "the original ground V uses barycentric interpolation");
    Near(ground.materials[1].uv.x, 0.25f, "the transformed inlay keeps its own UV");
    Near(ground.materials[1].uvPerX.x, 0.25f / 0.6f, "material UV gradients account for transformed scale");
}

void TestPortalGroundSlabs() {
    const auto slabs = BuildPortalGroundSlabs();
    Require(slabs.size() < 1500, "the fracture pattern uses fewer, more widely spaced source cells");
    float minArea = std::numeric_limits<float>::max(), maxArea = 0.0f, totalArea = 0.0f;
    bool hasLongPolygon = false;
    constexpr float limit = PortalGroundSurface::kResolution - 1;
    for (const auto& slab : slabs) {
        const auto count = size_t(slab.centerAndHinge.z);
        const auto hinge = size_t(slab.centerAndHinge.w);
        Require(count >= 3 && count <= PortalGroundSlab::kMaxCorners && hinge < count,
            "slab records fit the GPU layout and select a valid hinge");
        hasLongPolygon |= count >= 6;
        float doubleArea = 0.0f;
        for (size_t i = 0; i < count; ++i) {
            const auto a = slab.corners[i], b = slab.corners[(i + 1) % count];
            Require(a.x >= -0.001f && a.x <= limit + 0.001f && a.y >= -0.001f && a.y <= limit + 0.001f,
                "slab stays inside the ground sampling domain");
            doubleArea += a.x * b.y - a.y * b.x;
        }
        const float area = 0.5f * doubleArea;
        Require(area > 0.05f, "every slab has a nondegenerate face and consistent winding");
        minArea = std::min(minArea, area);
        maxArea = std::max(maxArea, area);
        totalArea += area;
    }
    Require(hasLongPolygon && maxArea > minArea * 4.0f, "the fractures contain varied sizes and polygon shapes");
    Require(std::abs(totalArea - limit * limit) < 0.005f * limit * limit,
        "the nearby floor remains partitioned without systematic gaps or overlap");
    PortalWaveSettings settings;
    Near(settings.Visual({glm::vec3(0.0f), 0.3f}).groundThickness, 0.16f,
        "the authored slab thickness reaches the active wave");
}

void TestPortalShardsFollowCameraDistance() {
    PortalShardEmitter effect;
    effect.followRider = true;
    effect.count = 1;
    effect.emissionDuration = effect.fadeIn = 0.0f;
    effect.spawnRadius = glm::vec2(4.0f);
    effect.travelDistance = effect.spawnHeight = effect.riseDistance = glm::vec2(0.0f);
    effect.size = glm::vec2(0.8f);
    effect.orbitSpeed = 0.0f;
    const glm::vec3 rider(5.0f, 2.0f, -10.0f);
    const glm::vec3 riderCenter = rider + glm::vec3(0.0f, 1.1f, 0.0f);
    const auto origin = glm::translate(glm::mat4(1.0f), rider);
    const auto shardsFrom = [&](const glm::vec3& eye) {
        std::vector<PortalShardVisual> shards;
        AppendPortalShardVisuals(effect, 0.75f, origin, shards, eye);
        return shards;
    };
    const auto distanceToRider = [&](const PortalShardVisual& shard) {
        return glm::length(glm::vec3(shard.transform[3]) - rider);
    };
    const auto size = [](const PortalShardVisual& shard) {
        return glm::length(glm::vec3(shard.transform[0]));
    };
    float previousRadius = 0.0f;
    float previousSize = 0.0f;
    float previousSpacing = 0.0f;
    for (float distance : {0.75f, 2.0f, 5.0f, 20.0f, 70.0f}) {
        const auto shards = shardsFrom(riderCenter + glm::vec3(0.0f, 0.0f, distance));
        Require(shards.size() == 1, "a shard outside the rider's silhouette remains visible");
        const float radius = distanceToRider(shards.front());
        const float meshSize = size(shards.front());
        Require(radius > previousRadius && meshSize > previousSize && radius / meshSize > previousSpacing,
            "camera pullback increases radius and size while giving the burst more spacing");
        if (distance == 5.0f) {
            Near(radius, 4.0f, "reference camera distance preserves authored radius");
            Near(meshSize, 0.8f, "reference camera distance preserves authored size");
        }
        previousRadius = radius;
        previousSize = meshSize;
        previousSpacing = radius / meshSize;
    }

    const glm::vec3 farSideEye = riderCenter + glm::vec3(40.0f, 0.0f, 0.0f);
    effect.size = glm::vec2(0.2f);
    Require(shardsFrom(farSideEye).empty(), "camera pullback hides a shard entirely within the rider's sightline");
    Require(shardsFrom(riderCenter + glm::vec3(-40.0f, 0.0f, 0.0f)).empty(),
        "the clear area remains aligned after changing the viewing angle");
    effect.size = glm::vec2(0.8f);
    const auto overlapping = shardsFrom(farSideEye);
    Require(overlapping.size() == 1 && overlapping.front().riderClearance.weight == 1.0f,
        "a shard crossing the rider's silhouette survives for per-pixel masking");

    std::vector<PortalShardVisual> largeBurst;
    AppendPortalShardVisuals(PortalShardEmitter{}, 0.75f, origin, largeBurst,
        riderCenter + glm::vec3(0.0f, 0.0f, 12.0f));
    Require(largeBurst.size() == 12, "large default shards remain drawable when the camera pulls back");

    effect.followRider = false;
    const auto fixed = shardsFrom(farSideEye);
    Require(fixed.size() == 1, "fixed scene emitters keep their placement");
    Near(distanceToRider(fixed.front()), 4.0f, "fixed emitter keeps authored distance at any zoom");
    Near(size(fixed.front()), 0.8f, "fixed emitter keeps authored size at any zoom");
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
        {"portal opening shard emission and cleanup", TestPortalOpeningShards},
        {"portal screen filter follows opening and traversal", TestPortalScreenFilter},
        {"portal crossing wave starts at the exit and stays in world space", TestPortalCrossingWave},
        {"portal ground samples coarse terrain and ignores overhead and rider geometry", TestPortalGroundSurface},
        {"portal fractures partition the ground into irregular slabs", TestPortalGroundSlabs},
        {"portal shards adapt to camera distance and clear the rider", TestPortalShardsFollowCameraDistance},
        {"invalid frame durations", TestInvalidDelta},
        {"opaque clipping far sources and idempotence", TestOpaqueClippingAndClones},
        {"skinned attachments and inactive effects", TestSkinnedClonesAndInactiveEffects},
        {"replaceable scene source without ECS or GPU", TestSceneSourceContract},
    };
    for (const auto& test : tests) { test.run(); std::printf("PASS: %s\n", test.name); }
    std::printf("All %zu vehicle presentation groups passed.\n", sizeof(tests) / sizeof(tests[0]));
}
