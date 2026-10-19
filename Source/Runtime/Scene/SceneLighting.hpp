#pragma once
#include "../Renderer/RenderUtilities/light.hpp"
#include <vector>
#include <string>
#include <tuple>

namespace engine {
struct SceneLightCandidate {
    uint64_t id;
    std::string name;
    glm::mat4 world;
    LightComponent light;
    bool visible = true;
};
struct SceneLightingData {
    std::vector<GpuLight> lights;
    DirectionalLightState directional;
};
inline SceneLightingData BuildSceneLighting(std::vector<SceneLightCandidate> candidates) {
    candidates.erase(std::remove_if(candidates.begin(), candidates.end(), [](const auto& c) {
        return !c.visible || !c.light.IsValid();
    }), candidates.end());
    // Keep primary selection stable when ECS archetypes/query order change.
    std::sort(candidates.begin(), candidates.end(), [](const auto& a, const auto& b) { return a.id < b.id; });
    const SceneLightCandidate* primary = nullptr;
    for (const auto& c : candidates) {
        if (c.light.type != LightType::Directional) continue;
        const auto rank = [](const auto& light) { return std::tuple{!light.light.primaryDirectional, light.name != "MainSun", light.id}; };
        if (!primary || rank(c) < rank(*primary)) primary = &c;
    }
    SceneLightingData result;
    if (primary) {
        result.directional = {primary->id, MakeGpuLight(primary->light, primary->world),
            primary->light.shadow, primary->light.castsShadows && primary->light.intensity > 0};
        result.directional.light.params.w = result.directional.castsShadows ? 1.0f : 0.0f;
        result.lights.push_back(result.directional.light);
    }
    bool spotAssigned = false;
    for (const auto& c : candidates) {
        if (primary && c.id == primary->id) continue;
        if (result.lights.size() >= MAX_LIGHTS) break;
        auto gpu = MakeGpuLight(c.light, c.world);
        if (!spotAssigned && c.light.type == LightType::Spot && c.light.castsShadows && c.light.intensity > 0) {
            gpu.params.w = 2.0f;
            spotAssigned = true;
        }
        result.lights.push_back(gpu);
    }
    return result;
}
} // namespace engine
