#include "Runtime/Scene/SceneLighting.hpp"
#include "Runtime/Scene/LightSerialization.hpp"
#include "Runtime/Project/SceneSnapshotValidation.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <cstdio>
#include <cstdlib>

using namespace engine;
static void Require(bool ok, const char* message) { if (!ok) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); } }
static void Near(float actual, float expected, const char* message) { Require(std::abs(actual - expected) < 0.0001f, message); }
static void SameVector(glm::vec3 a, glm::vec3 b, const char* message) { Require(glm::length(a - b) < 0.0001f, message); }
static void Finite(const ShadowData& shadow) {
    for (const auto& m : shadow.lightVP) for (int c = 0; c < 4; ++c) for (int r = 0; r < 4; ++r)
        Require(std::isfinite(m[c][r]), "cascade matrix remains finite");
}
int main() {
    LightComponent sun;
    sun.type = LightType::Directional; sun.range = 0; sun.primaryDirectional = true;
    sun.direction = NormalizeLightDirection({0.5f, -1, 0.3f});
    const auto parent = glm::translate(glm::mat4(1), {100, 200, 300}) * glm::rotate(glm::mat4(1), 0.7f, glm::vec3(0, 1, 0));
    auto lighting = BuildSceneLighting({{42, "MainSun", parent, sun}});
    SameVector(glm::vec3(lighting.lights[0].position), -NormalizeLightDirection(glm::vec3(parent * glm::vec4(sun.direction, 0))),
        "direct lighting uses the transformed ray direction, never translation");
    const auto view = glm::lookAt(glm::vec3(0, 3, 10), glm::vec3(0), glm::vec3(0, 1, 0));
    auto frame = BuildDirectionalShadowFrame(lighting.directional, view, glm::radians(60.0f), 1.6f, 0.1f, 1000);
    SameVector(glm::vec3(frame.source.position), glm::vec3(lighting.lights[0].position), "illumination and CSM share exactly one direction");
    for (int cascade = 0; cascade < 3; ++cascade) {
        const auto projectedRay = frame.cascades.lightVP[cascade] * glm::vec4(glm::vec3(frame.source.position), 0);
        Near(projectedRay.x, 0, "toward-light direction is parallel to shadow projection depth");
        Near(projectedRay.y, 0, "toward-light direction has no shadow-plane displacement");
        Require(projectedRay.z < 0, "moving toward the light decreases shadow depth");
    }
    Finite(frame.cascades);
    sun.direction = NormalizeLightDirection({-1, -1, 0.8f});
    const auto changed = BuildSceneLighting({{42, "MainSun", parent, sun}});
    const auto changedFrame = BuildDirectionalShadowFrame(changed.directional, view, glm::radians(60.0f), 1.6f, 0.1f, 1000);
    Require(glm::length(glm::vec3(changedFrame.source.position - frame.source.position)) > 0.2f,
        "editing sun direction immediately changes the illumination source");
    Require(changedFrame.cascades.lightVP[0] != frame.cascades.lightVP[0], "editing sun direction changes cascade matrices in the same frame");
    std::puts("PASS transformed sun direction and shadow projection agree before and after edits");

    std::vector<SceneLightCandidate> many;
    LightComponent fill;
    for (int i = 0; i < 30; ++i) many.push_back({uint64_t(i + 1), "fill", glm::mat4(1), fill});
    many.push_back({100, "MainSun", parent, sun});
    auto otherSun = sun; otherSun.primaryDirectional = false;
    many.push_back({101, "SecondarySun", glm::mat4(1), otherSun});
    auto budget = BuildSceneLighting(many);
    Require(budget.lights.size() == 16 && budget.directional.entityId == 100 && budget.lights[0].params.w == 1,
        "primary sun is retained when more than sixteen lights exist");
    auto secondary = BuildSceneLighting({many.back(), many[many.size()-2]});
    Require(secondary.lights[0].params.w == 1 && secondary.lights[1].params.w == 0, "secondary directional light never shares primary shadows");
    auto spot = fill; spot.type = LightType::Spot;
    auto spots = BuildSceneLighting({{1,"first",glm::mat4(1),spot},{2,"second",glm::mat4(1),spot}});
    Require(spots.lights[0].params.w == 2 && spots.lights[1].params.w == 0, "spot shadow assignment is explicit");
    auto disabled = sun; disabled.castsShadows = false;
    auto off = BuildSceneLighting({{42,"MainSun",parent,disabled}});
    auto offFrame = BuildDirectionalShadowFrame(off.directional, view, glm::radians(60.0f), 1, 0.1f, 1000);
    Require(off.lights[0].params.w == 0 && offFrame.parameters.raster.z == 0 && offFrame.cascades.cascadeSplits.z == 0,
        "disabling sun shadows removes sampling and shadow projection");
    Require(BuildSceneLighting({{42,"MainSun",parent,sun,false}}).directional.entityId == 0, "hidden sun has no fixed shadow fallback");
    std::puts("PASS stable primary selection, GPU budget and explicit shadow ownership");

    sun.shadow.firstSplit = 8; sun.shadow.secondSplit = 30; sun.shadow.distance = 200;
    sun.shadow.receiverBias = 0.002f; sun.shadow.filterRadius = 4; sun.shadow.blendFraction = 0;
    sun.shadow.strength = 0.4f; sun.shadow.constantBias = 2; sun.shadow.slopeBias = 3;
    lighting = BuildSceneLighting({{42,"MainSun",parent,sun}});
    frame = BuildDirectionalShadowFrame(lighting.directional, view, glm::radians(60.0f), 1, 0.1f, 100);
    Near(frame.cascades.cascadeSplits.x, 8, "first split uses authored setting");
    Near(frame.cascades.cascadeSplits.y, 30, "second split uses authored setting");
    Near(frame.cascades.cascadeSplits.z, 100, "shadow distance respects actual far clip");
    Near(frame.parameters.sampling.x, 0.002f, "receiver bias reaches GPU parameters");
    Near(frame.parameters.sampling.w, 0.4f, "shadow strength reaches GPU parameters");
    Near(frame.parameters.raster.y, 3, "raster slope bias reaches GPU parameters");
    auto clipped = BuildDirectionalShadowFrame(lighting.directional, view, glm::radians(60.0f), 1, 90, 100);
    Require(clipped.cascades.cascadeSplits.x > 90 && clipped.cascades.cascadeSplits.x < clipped.cascades.cascadeSplits.y,
        "large camera near plane cannot invert cascade intervals");
    Finite(clipped.cascades);
    auto narrow = BuildDirectionalShadowFrame(lighting.directional, view, glm::radians(60.0f), 1, 100, std::nextafter(100.0f, 200.0f));
    Finite(narrow.cascades);
    Require(narrow.parameters.raster.z == 0, "unrepresentable cascade intervals are safely disabled");
    std::puts("PASS authored cascade/filter/depth controls and clipping safety");

    const auto saved = SerializeLight(sun);
    Require(SerializeLight(DeserializeLight(saved)) == saved, "light and all shadow parameters round trip");
    auto legacy = saved; legacy.erase("shadow"); legacy.erase("castsShadows"); legacy.erase("primaryDirectional");
    Require(DeserializeLight(legacy).shadow.distance == 800, "legacy light documents retain shadow defaults");
    for (const auto& bad : {nlohmann::json{{"shadow",{{"distance",10},{"secondSplit",70}}}},
        nlohmann::json{{"direction",{0,0,0}}}, nlohmann::json{{"castsShadows","yes"}},
        nlohmann::json{{"shadow",{{"receiverBias",-1}}}}}) {
        auto invalid = saved; invalid.update(bad);
        bool rejected = false;
        try { (void)DeserializeLight(invalid); } catch (const std::exception&) { rejected = true; }
        Require(rejected, "bad light/shadow settings fail before scene mutation");
    }
    nlohmann::json snapshot{{"Entities", {{{"Name","MainSun"},{"Position",{0,0,0}},{"Rotation",{0,0,0}},
        {"Scale",{1,1,1}},{"Light",saved},{"Visible",true}}}}};
    ValidateSceneSnapshot(snapshot);
    std::puts("PASS native/generic serialization compatibility and atomic invalid-setting rejection");
}
